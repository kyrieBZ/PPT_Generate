CREATE TABLE IF NOT EXISTS users (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  username VARCHAR(50) NOT NULL UNIQUE,
  email VARCHAR(120) NOT NULL UNIQUE,
  password_hash CHAR(64) NOT NULL,
  salt CHAR(32) NOT NULL,
  is_disabled TINYINT(1) NOT NULL DEFAULT 0,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  last_login TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS auth_tokens (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  user_id BIGINT UNSIGNED NOT NULL,
  token CHAR(64) NOT NULL UNIQUE,
  expires_at TIMESTAMP NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  INDEX idx_auth_tokens_token (token),
  INDEX idx_auth_tokens_user (user_id),
  CONSTRAINT fk_auth_tokens_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS password_reset_codes (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  user_id BIGINT UNSIGNED NOT NULL,
  code_hash CHAR(64) NOT NULL,
  expires_at TIMESTAMP NOT NULL,
  used_at TIMESTAMP NULL DEFAULT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  INDEX idx_password_reset_user (user_id),
  INDEX idx_password_reset_expires (expires_at),
  CONSTRAINT fk_password_reset_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS ppt_requests (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  user_id BIGINT UNSIGNED NOT NULL,
  title VARCHAR(200) NOT NULL,
  topic TEXT NOT NULL,
  pages INT NOT NULL,
  style VARCHAR(50) NOT NULL,
  include_images TINYINT(1) NOT NULL DEFAULT 0,
  include_charts TINYINT(1) NOT NULL DEFAULT 0,
  include_notes TINYINT(1) NOT NULL DEFAULT 0,
  model_key VARCHAR(64) NOT NULL DEFAULT 'qwen-turbo',
  model_name VARCHAR(120) NOT NULL DEFAULT '通义千问 · 极速模型',
  template_id VARCHAR(120) NOT NULL DEFAULT '',
  template_name VARCHAR(200) NOT NULL DEFAULT '',
  status VARCHAR(20) NOT NULL DEFAULT 'completed',
  output_path VARCHAR(512) NOT NULL DEFAULT '',
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  INDEX idx_ppt_requests_user (user_id),
  CONSTRAINT fk_ppt_requests_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- materials 表：存储用户上传素材及提取结果，review_result 由管理员 AI 审核写入
CREATE TABLE IF NOT EXISTS materials (
  id             VARCHAR(64)  NOT NULL,
  user_id        BIGINT UNSIGNED NOT NULL,
  filename       VARCHAR(255) NOT NULL,
  file_type      VARCHAR(20)  NOT NULL,
  file_path      VARCHAR(512) NOT NULL,
  file_size      BIGINT UNSIGNED NOT NULL DEFAULT 0,
  status         VARCHAR(20)  NOT NULL DEFAULT 'pending',
  extract_result MEDIUMTEXT,
  error_msg      TEXT,
  review_result  TEXT         NULL DEFAULT NULL COMMENT 'JSON: {result, reason, reviewed_at}',
  created_at     TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at     TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  INDEX idx_materials_user (user_id),
  INDEX idx_materials_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 已有 materials 表时，为其补充 review_result 列（迁移语句）
-- ALTER TABLE materials ADD COLUMN review_result TEXT NULL DEFAULT NULL COMMENT 'JSON: {result, reason, reviewed_at}' AFTER error_msg;

-- 管理员删除素材时向用户发送的通知
CREATE TABLE IF NOT EXISTS material_deletion_notices (
  id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  user_id       BIGINT UNSIGNED NOT NULL,
  filename      VARCHAR(255)    NOT NULL,
  file_type     VARCHAR(20)     NOT NULL DEFAULT '',
  file_size     BIGINT UNSIGNED NOT NULL DEFAULT 0,
  delete_reason VARCHAR(1000)   NOT NULL DEFAULT '',
  deleted_by    VARCHAR(50)     NOT NULL DEFAULT 'admin',
  is_read       TINYINT(1)      NOT NULL DEFAULT 0,
  created_at    TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  INDEX idx_notice_user (user_id),
  INDEX idx_notice_user_read (user_id, is_read)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='管理员删除素材通知';

-- RAG 知识库功能说明：
-- 素材向量化索引状态由 Qdrant（user_knowledge collection）维护，无需 MySQL 额外字段。
-- 若需在 materials 表追踪索引状态，可执行如下迁移（可选）：
-- ALTER TABLE materials
--   ADD COLUMN rag_indexed    TINYINT(1)      NOT NULL DEFAULT 0  COMMENT 'RAG 向量化是否完成',
--   ADD COLUMN rag_indexed_at TIMESTAMP       NULL DEFAULT NULL   COMMENT 'RAG 索引时间',
--   ADD INDEX  idx_materials_rag (rag_indexed);
--
-- 上述字段当前实现中未使用（索引状态由 Qdrant count API 查询），若扩展可自行迁移。

-- 系统公告表
CREATE TABLE IF NOT EXISTS announcements (
  id         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  title      VARCHAR(200)    NOT NULL,
  content    TEXT            NOT NULL,
  is_pinned  TINYINT(1)      NOT NULL DEFAULT 0,
  starts_at  TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at TIMESTAMP       NULL DEFAULT NULL,
  created_by BIGINT UNSIGNED NOT NULL,
  created_at TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  INDEX idx_announcements_active (starts_at, expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='系统公告';
