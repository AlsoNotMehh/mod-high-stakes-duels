--
-- High Stakes Duels Database Schema
--

CREATE DATABASE IF NOT EXISTS `Custom` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `Custom`.`duel_normal_history` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `duration_ms` INT UNSIGNED NOT NULL DEFAULT '0',
  `winner_guid` INT UNSIGNED NOT NULL,
  `winner_name` VARCHAR(12) NOT NULL,
  `winner_race` TINYINT UNSIGNED NOT NULL,
  `winner_class` TINYINT UNSIGNED NOT NULL,
  `winner_level` TINYINT UNSIGNED NOT NULL,
  `loser_guid` INT UNSIGNED NOT NULL,
  `loser_name` VARCHAR(12) NOT NULL,
  `loser_race` TINYINT UNSIGNED NOT NULL,
  `loser_class` TINYINT UNSIGNED NOT NULL,
  `loser_level` TINYINT UNSIGNED NOT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_normal_winner` (`winner_guid`),
  KEY `idx_normal_loser` (`loser_guid`),
  KEY `idx_normal_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Completed standard duels';

CREATE TABLE IF NOT EXISTS `Custom`.`duel_gold_history` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `duration_ms` INT UNSIGNED NOT NULL DEFAULT '0',
  `winner_guid` INT UNSIGNED NOT NULL DEFAULT '0',
  `winner_name` VARCHAR(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT '',
  `winner_race` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `winner_class` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `winner_level` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `loser_guid` INT UNSIGNED NOT NULL DEFAULT '0',
  `loser_name` VARCHAR(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT '',
  `loser_race` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `loser_class` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `loser_level` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `stake_copper` INT UNSIGNED NOT NULL DEFAULT '0',
  `prize_copper` INT UNSIGNED NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `idx_gold_winner` (`winner_guid`),
  KEY `idx_gold_loser` (`loser_guid`),
  KEY `idx_gold_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Completed gold wager duels';

CREATE TABLE IF NOT EXISTS `Custom`.`duel_makgora_history` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `duration_ms` INT UNSIGNED NOT NULL DEFAULT '0',
  `winner_guid` INT UNSIGNED NOT NULL DEFAULT '0',
  `winner_account` INT UNSIGNED NOT NULL DEFAULT '0',
  `winner_name` VARCHAR(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT '',
  `winner_race` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `winner_class` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `winner_level` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `loser_guid` INT UNSIGNED NOT NULL DEFAULT '0',
  `loser_account` INT UNSIGNED NOT NULL DEFAULT '0',
  `loser_name` VARCHAR(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL DEFAULT '',
  `loser_race` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `loser_class` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `loser_level` TINYINT UNSIGNED NOT NULL DEFAULT '0',
  `map_id` SMALLINT UNSIGNED NOT NULL DEFAULT '0',
  `zone_id` INT UNSIGNED NOT NULL DEFAULT '0',
  `area_id` INT UNSIGNED NOT NULL DEFAULT '0',
  `position_x` FLOAT NOT NULL DEFAULT '0',
  `position_y` FLOAT NOT NULL DEFAULT '0',
  `position_z` FLOAT NOT NULL DEFAULT '0',
  `orientation` FLOAT NOT NULL DEFAULT '0',
  `status` VARCHAR(32) NOT NULL DEFAULT 'killed_and_locked',
  PRIMARY KEY (`id`),
  KEY `idx_makgora_winner` (`winner_guid`),
  KEY `idx_makgora_loser` (`loser_guid`),
  KEY `idx_makgora_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Hardcore Makgora death duel history';
