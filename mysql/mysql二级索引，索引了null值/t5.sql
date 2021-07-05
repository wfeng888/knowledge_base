CREATE TABLE `t5` (
  `id` int NOT NULL,
  `c1` int DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `c1` (`c1`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

insert into t5 values(1,1);
insert into t5 values(2,null);
insert into t5 values(3,3);
insert into t5 values(4,null);
insert into t5 values(5,5);
insert into t5 values(6,null);
flush tables;




CREATE TABLE `t8` (
  `id` int NOT NULL,
  `c1` int DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `c1` (`c1`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

insert into t8 values(1,1);
insert into t8 values(2,2);
insert into t8 values(3,3);
insert into t8 values(4,4);
insert into t8 values(5,5);
insert into t8 values(6,6);
flush tables;