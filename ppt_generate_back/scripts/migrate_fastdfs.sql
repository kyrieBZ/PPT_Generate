-- FastDFS 集成数据库迁移脚本
-- 执行：mysql -u ppt_user -p ppt_generate < scripts/migrate_fastdfs.sql

-- 添加 fastdfs_file_id 列（若已存在则跳过）
SET @col1 = (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'materials' AND COLUMN_NAME = 'fastdfs_file_id');
SET @sql1 = IF(@col1 = 0, 'ALTER TABLE materials ADD COLUMN fastdfs_file_id VARCHAR(256) DEFAULT NULL', 'SELECT ''fastdfs_file_id already exists''');
PREPARE stmt1 FROM @sql1; EXECUTE stmt1; DEALLOCATE PREPARE stmt1;

-- 添加 fastdfs_url 列（若已存在则跳过）
SET @col2 = (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'materials' AND COLUMN_NAME = 'fastdfs_url');
SET @sql2 = IF(@col2 = 0, 'ALTER TABLE materials ADD COLUMN fastdfs_url VARCHAR(512) DEFAULT NULL', 'SELECT ''fastdfs_url already exists''');
PREPARE stmt2 FROM @sql2; EXECUTE stmt2; DEALLOCATE PREPARE stmt2;

-- 添加 storage_type 列（若已存在则跳过）
SET @col3 = (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'materials' AND COLUMN_NAME = 'storage_type');
SET @sql3 = IF(@col3 = 0, 'ALTER TABLE materials ADD COLUMN storage_type VARCHAR(16) NOT NULL DEFAULT ''local''', 'SELECT ''storage_type already exists''');
PREPARE stmt3 FROM @sql3; EXECUTE stmt3; DEALLOCATE PREPARE stmt3;

UPDATE materials SET storage_type = 'local' WHERE storage_type IS NULL OR storage_type = '';

CREATE TABLE IF NOT EXISTS template_fastdfs_map (
  template_id        VARCHAR(64)   NOT NULL,
  pptx_file_id       VARCHAR(256)  DEFAULT NULL,
  pptx_url           VARCHAR(512)  DEFAULT NULL,
  thumbnail_file_id  VARCHAR(256)  DEFAULT NULL,
  thumbnail_url      VARCHAR(512)  DEFAULT NULL,
  analysis_file_id   VARCHAR(256)  DEFAULT NULL,
  analysis_url       VARCHAR(512)  DEFAULT NULL,
  uploaded_at        DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at         DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (template_id),
  INDEX idx_uploaded_at (uploaded_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

SELECT 'migration done' AS status;
