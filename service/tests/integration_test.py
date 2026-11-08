from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from contextlib import closing, contextmanager


TEST_EMAIL = "aura-cpp-integration@tensor.local"
TEST_PASSWORD = "AURA-Cpp-Test-2026!"
TEST_PASSWORD_HASH = "$2b$10$P9Cfh6nCEOIHjca1c4b/4OdhEqqoxgEDjrBW34uC46QQfXkNvb0dq"
TEST_USER_ID = "aura-cpp-integration-user"
RECORD_IDS = ["aura-cpp-integration-record-1", "aura-cpp-integration-record-2"]
JWT_SECRET = "aura-cpp-integration-secret-at-least-32-characters"


@contextmanager
def removable_temporary_directory():
    path = Path(tempfile.mkdtemp(prefix="aura-drogon-test-"))
    try:
        yield path
    finally:
        last_error = None
        for _ in range(30):
            try:
                shutil.rmtree(path)
                return
            except PermissionError as error:
                last_error = error
                time.sleep(0.1)
        if last_error:
            raise last_error


def request(base_url: str, method: str, path: str, token: str = "", body=None):
    headers = {"Accept": "application/json"}
    data = None
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if body is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(base_url + path, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode("utf-8"))


def request_text(base_url: str, path: str):
    req = urllib.request.Request(base_url + path, headers={"Accept": "text/html"}, method="GET")
    with urllib.request.urlopen(req, timeout=5) as response:
        return response.status, response.read().decode("utf-8")


def encode_segment(value) -> str:
    raw = json.dumps(value, separators=(",", ":")).encode("utf-8")
    return base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")


def legacy_hs256_token() -> str:
    now = int(time.time())
    unsigned = ".".join(
        (
            encode_segment({"alg": "HS256", "typ": "JWT"}),
            encode_segment({"userId": TEST_USER_ID, "role": "employee", "iat": now, "exp": now + 3600}),
        )
    )
    signature = hmac.new(JWT_SECRET.encode(), unsigned.encode(), hashlib.sha256).digest()
    return unsigned + "." + base64.urlsafe_b64encode(signature).decode("ascii").rstrip("=")


def has_valid_hs256_signature(token: str) -> bool:
    unsigned, supplied = token.rsplit(".", 1)
    expected = base64.urlsafe_b64encode(
        hmac.new(JWT_SECRET.encode(), unsigned.encode(), hashlib.sha256).digest()
    ).decode("ascii").rstrip("=")
    return hmac.compare_digest(supplied, expected)


def backup_and_seed(source: Path, target: Path) -> str:
    with closing(sqlite3.connect(source)) as live, closing(sqlite3.connect(target)) as test:
        live.backup(test)
        test.execute(
            """
            INSERT OR REPLACE INTO users
              (id,name,employee_id,department_id,role,email,phone,password_hash,status,created_at,updated_at)
            VALUES (?,?,?,?,?,?,?,?,?,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)
            """,
            (
                TEST_USER_ID,
                "AURA C++ Integration",
                "CPP-INTEGRATION",
                None,
                "employee",
                TEST_EMAIL,
                None,
                TEST_PASSWORD_HASH,
                "active",
            ),
        )
        device = test.execute("SELECT id FROM devices ORDER BY created_at LIMIT 1").fetchone()
        if device is None:
            device_id = "aura-cpp-integration-device"
            test.execute(
                """
                INSERT INTO devices
                  (id,name,serial_number,model,room_id,status,firmware_version,created_at,updated_at)
                VALUES (?,?,?,?,?,'online','test',CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)
                """,
                (device_id, "C++ Integration Device", "CPP-INTEGRATION-001", "test", None),
            )
        else:
            device_id = str(device[0])
        test.executemany("DELETE FROM focus_records WHERE id=?", ((item,) for item in RECORD_IDS))
        test.commit()
        return device_id


def main() -> int:
    project = Path(__file__).resolve().parents[1]
    workspace = project.parent
    service = project / "build" / "Release" / "AuraService.exe"
    source_database = workspace / "backend" / "database" / "aura.db"
    ui_root = workspace / "cpp_desktop" / "build" / "Release" / "ui"
    if not service.is_file() or not source_database.is_file():
        raise RuntimeError("Build AuraService and ensure backend/database/aura.db exists first")

    with removable_temporary_directory() as directory:
        test_database = directory / "aura-test.db"
        device_id = backup_and_seed(source_database, test_database)
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        base_url = f"http://127.0.0.1:{port}"
        environment = os.environ.copy()
        environment.update(
            {
                "AURA_DB_PATH": str(test_database),
                "AURA_BIND": "127.0.0.1",
                "AURA_PORT": str(port),
                "JWT_SECRET": JWT_SECRET,
            }
        )
        if ui_root.is_dir():
            environment["AURA_UI_ROOT"] = str(ui_root)
        creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        process = subprocess.Popen(
            [str(service)],
            cwd=workspace,
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            creationflags=creation_flags,
        )
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    detail = process.stderr.read() if process.stderr else ""
                    raise RuntimeError(f"AuraService exited early: {detail}")
                try:
                    status, health = request(base_url, "GET", "/api/v1/health")
                    if status == 200 and health["data"]["status"] == "ok":
                        break
                except OSError:
                    time.sleep(0.1)
            else:
                raise RuntimeError("AuraService did not become healthy")

            ui_verified = False
            if ui_root.is_dir():
                status, index_html = request_text(base_url, "/index.html")
                assert status == 200
                assert all(
                    f'id="tab-{tab}"' in index_html
                    for tab in ("dashboard", "waveform", "debug", "spatial", "alerts", "users")
                )
                ui_verified = True

            status, rejected = request(base_url, "GET", "/api/v1/auth/me", token="invalid")
            assert status == 403 and rejected["success"] is False
            status, legacy_profile = request(
                base_url, "GET", "/api/v1/auth/me", token=legacy_hs256_token()
            )
            assert status == 200 and legacy_profile["data"]["id"] == TEST_USER_ID

            status, login = request(
                base_url,
                "POST",
                "/api/v1/auth/login",
                body={"email": TEST_EMAIL, "password": TEST_PASSWORD},
            )
            assert status == 200 and login["success"] is True
            token = login["data"]["token"]
            assert has_valid_hs256_signature(token)

            status, profile = request(base_url, "GET", "/api/v1/auth/me", token=token)
            assert status == 200 and profile["data"]["id"] == TEST_USER_ID

            status, devices = request(base_url, "GET", "/api/v1/devices", token=token)
            assert status == 200 and any(item["id"] == device_id for item in devices["data"])

            records = [
                {
                    "recordId": RECORD_IDS[0],
                    "deviceId": device_id,
                    "focusScore": 82.5,
                    "heartRate": 71.0,
                    "respirationRate": 16.0,
                    "recordedAt": "2099-01-01T00:00:02.000Z",
                    "processingMode": "cpp-integration",
                    "researchDiagnostics": {"source": "isolated-test"},
                },
                {
                    "recordId": RECORD_IDS[1],
                    "deviceId": device_id,
                    "focusScore": 76.0,
                    "heartRate": 73.0,
                    "respirationRate": 17.0,
                    "recordedAt": "2099-01-01T00:00:01.000Z",
                    "processingMode": "cpp-integration",
                },
            ]
            status, inserted = request(
                base_url, "POST", "/api/v1/focus-data/bulk-ingest", token, {"records": records}
            )
            assert status == 201 and inserted["data"]["inserted"] == 2
            status, duplicate = request(
                base_url, "POST", "/api/v1/focus-data/bulk-ingest", token, {"records": records}
            )
            assert status == 201 and duplicate["data"]["inserted"] == 0

            status, latest = request(base_url, "GET", "/api/v1/focus-data/latest?limit=5", token)
            assert status == 200 and any(item["id"] in RECORD_IDS for item in latest["data"])
            status, dashboard = request(base_url, "GET", "/api/v1/mobile/dashboard", token)
            assert status == 200 and dashboard["data"]["user"]["id"] == TEST_USER_ID
            status, alerts = request(base_url, "GET", "/api/v1/alerts", token)
            assert status == 200 and isinstance(alerts["data"], list)
            status, chat = request(
                base_url, "POST", "/api/v1/mobile/chat", token, {"message": "当前心率是多少？"}
            )
            assert status == 200 and "71" in chat["data"]["reply"]
            print(
                "aura_integration_test: passed "
                "(health/ui/auth/jwt-compat/devices/ingest/latest/dashboard/alerts/chat)"
                if ui_verified
                else "aura_integration_test: passed "
                "(health/auth/jwt-compat/devices/ingest/latest/dashboard/alerts/chat; ui skipped)"
            )
            return 0
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            if process.stderr:
                process.stderr.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"aura_integration_test: failed: {error}", file=sys.stderr)
        raise
