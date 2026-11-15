-- AURA 服务端表结构
-- 用法: sqlite3 /opt/aura/app/data/aura.db < deploy/schema.sql
-- 服务启动时只建库文件、不建表，缺表会导致除 /health 外接口全部报 no such table
CREATE TABLE departments (
      id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      code TEXT UNIQUE NOT NULL,
      member_count INTEGER DEFAULT 0,
      average_focus REAL DEFAULT 0,
      created_at TEXT DEFAULT CURRENT_TIMESTAMP
    );
CREATE TABLE rooms (
      id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      type TEXT NOT NULL CHECK(type IN ('meeting', 'office', 'rest', 'focus')),
      floor INTEGER DEFAULT 1,
      capacity INTEGER DEFAULT 0,
      current_occupancy INTEGER DEFAULT 0,
      average_focus REAL DEFAULT 0,
      coordinate_x REAL DEFAULT 0,
      coordinate_y REAL DEFAULT 0,
      width REAL DEFAULT 100,
      height REAL DEFAULT 100,
      created_at TEXT DEFAULT CURRENT_TIMESTAMP
    , department_id TEXT REFERENCES departments(id) ON DELETE SET NULL);
CREATE TABLE devices (
      id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      serial_number TEXT UNIQUE NOT NULL,
      model TEXT NOT NULL,
      room_id TEXT REFERENCES rooms(id) ON DELETE SET NULL,
      status TEXT DEFAULT 'offline' CHECK(status IN ('online', 'offline', 'error')),
      firmware_version TEXT DEFAULT '1.0.0',
      last_seen_at TEXT,
      created_at TEXT DEFAULT CURRENT_TIMESTAMP,
      updated_at TEXT DEFAULT CURRENT_TIMESTAMP
    , endpoint_url TEXT);
CREATE TABLE users (
      id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      employee_id TEXT UNIQUE NOT NULL,
      department_id TEXT REFERENCES departments(id) ON DELETE SET NULL,
      role TEXT DEFAULT 'employee' CHECK(role IN ('admin', 'manager', 'employee')),
      email TEXT UNIQUE NOT NULL,
      phone TEXT,
      status TEXT DEFAULT 'active' CHECK(status IN ('active', 'inactive')),
      created_at TEXT DEFAULT CURRENT_TIMESTAMP,
      updated_at TEXT DEFAULT CURRENT_TIMESTAMP
    , password_hash TEXT, avatar TEXT);
CREATE TABLE focus_records (
      id TEXT PRIMARY KEY,
      user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
      device_id TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
      room_id TEXT REFERENCES rooms(id) ON DELETE SET NULL,
      focus_score REAL NOT NULL CHECK(focus_score >= 0 AND focus_score <= 100),
      stress_level REAL CHECK(stress_level >= 0 AND stress_level <= 100),
      fatigue_level REAL CHECK(fatigue_level >= 0 AND fatigue_level <= 100),
      heart_rate REAL,
      respiration_rate REAL,
      recorded_at TEXT DEFAULT CURRENT_TIMESTAMP
    , nn_heart_rate REAL, nn_respiration_rate REAL, nn_focus_score REAL, c_heart_rate REAL, c_respiration_rate REAL, c_focus_score REAL, nn_quality REAL, ppi_heart_rate REAL, median_ppi_ms REAL, gwo_vmd_heart_rate REAL, gwo_vmd_quality REAL, gwo_vmd_alpha REAL, gwo_vmd_processing_ms REAL, gwo_vmd_harmonic_risk REAL, ppi_beat_count INTEGER, gwo_vmd_k INTEGER, processing_mode TEXT, research_diagnostics TEXT, relax_score REAL);
CREATE TABLE alerts (
      id TEXT PRIMARY KEY,
      type TEXT NOT NULL CHECK(type IN ('low_focus', 'high_stress', 'fatigue', 'device_offline')),
      severity TEXT NOT NULL CHECK(severity IN ('low', 'medium', 'high')),
      user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
      device_id TEXT REFERENCES devices(id) ON DELETE SET NULL,
      room_id TEXT REFERENCES rooms(id) ON DELETE SET NULL,
      message TEXT NOT NULL,
      is_resolved BOOLEAN DEFAULT 0,
      resolved_at TEXT,
      created_at TEXT DEFAULT CURRENT_TIMESTAMP
    );
CREATE INDEX idx_focus_records_user ON focus_records(user_id);
CREATE INDEX idx_focus_records_device ON focus_records(device_id);
CREATE INDEX idx_focus_records_recorded_at ON focus_records(recorded_at);
CREATE INDEX idx_alerts_created_at ON alerts(created_at);
CREATE INDEX idx_devices_room ON devices(room_id);
CREATE TABLE oauth_accounts (
      id TEXT PRIMARY KEY,
      user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      provider TEXT NOT NULL CHECK(provider IN ('github', 'qq', 'wechat')),
      provider_account_id TEXT NOT NULL,
      access_token TEXT,
      refresh_token TEXT,
      expires_at TEXT,
      created_at TEXT DEFAULT CURRENT_TIMESTAMP,
      updated_at TEXT DEFAULT CURRENT_TIMESTAMP,
      UNIQUE(provider, provider_account_id)
    );
CREATE INDEX idx_oauth_accounts_user ON oauth_accounts(user_id);
CREATE INDEX idx_oauth_accounts_provider ON oauth_accounts(provider, provider_account_id);
CREATE TABLE predictions (
      id TEXT PRIMARY KEY,
      user_id TEXT REFERENCES users(id) ON DELETE SET NULL,
      device_id TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
      horizon_minutes REAL NOT NULL,
      current_state TEXT NOT NULL,
      trajectory TEXT NOT NULL,
      mind_wander TEXT NOT NULL,
      fatigue_critical TEXT NOT NULL,
      interventions TEXT NOT NULL,
      overall_risk REAL NOT NULL,
      optimal_window TEXT NOT NULL,
      created_at TEXT DEFAULT CURRENT_TIMESTAMP
    );
CREATE INDEX idx_predictions_device ON predictions(device_id);
CREATE INDEX idx_predictions_user ON predictions(user_id);
CREATE INDEX idx_predictions_created_at ON predictions(created_at);
CREATE TABLE schema_migrations (
      version INTEGER PRIMARY KEY,
      name TEXT NOT NULL,
      applied_at TEXT DEFAULT CURRENT_TIMESTAMP
    );
CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at TEXT);
