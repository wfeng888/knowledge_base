CREATE TABLE `t5` (
  `id` int NOT NULL,
  `c1` int DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `c1` (`c1`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

(root@localhost:3300.socket) [wftest]> insert into t5 values(1,1);
Query OK, 1 row affected (0.00 sec)

(root@localhost:3300.socket) [wftest]> insert into t5 values(2,null);
Query OK, 1 row affected (0.00 sec)

(root@localhost:3300.socket) [wftest]> insert into t5 values(3,3);
Query OK, 1 row affected (0.00 sec)

(root@localhost:3300.socket) [wftest]> insert into t5 values(4,null);
Query OK, 1 row affected (0.00 sec)

(root@localhost:3300.socket) [wftest]> insert into t5 values(5,5);
Query OK, 1 row affected (0.00 sec)

(root@localhost:3300.socket) [wftest]> insert into t5 values(6,null);
Query OK, 1 row affected (0.00 sec)

(root@localhost:3300.socket) [wftest]> flush tables;
Query OK, 0 rows affected (0.01 sec)
