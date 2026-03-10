CREATE DATABASE IF NOT EXISTS `kd39` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE `kd39`;

CREATE TABLE IF NOT EXISTS `config_entries` (
    `id`             BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    `namespace_name` VARCHAR(128)    NOT NULL DEFAULT '',
    `key`            VARCHAR(256)    NOT NULL DEFAULT '',
    `value`          MEDIUMTEXT      NOT NULL,
    `version`        BIGINT          NOT NULL DEFAULT 0,
    `environment`    VARCHAR(32)     NOT NULL DEFAULT 'dev',
    `created_at`     DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at`     DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY `uk_ns_key_env` (`namespace_name`, `key`, `environment`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `users` (
    `user_id`    VARCHAR(64)     NOT NULL PRIMARY KEY,
    `nickname`   VARCHAR(128)    NOT NULL DEFAULT '',
    `avatar`     VARCHAR(512)    NOT NULL DEFAULT '',
    `level`      INT             NOT NULL DEFAULT 1,
    `created_at` DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
