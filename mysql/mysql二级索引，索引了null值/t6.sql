(root@localhost:3300.socket) [wftest]> create table t6 like t4;
Query OK, 0 rows affected (0.01 sec)

(root@localhost:3300.socket) [wftest]> insert into t6 values(1,1);
Query OK, 1 row affected (0.00 sec)

(root@localhost:3300.socket) [wftest]> insert into t6 values(2,2);
Query OK, 1 row affected (0.00 sec)

(root@localhost:3300.socket) [wftest]> insert into t6 values(3,4);
Query OK, 1 row affected (0.01 sec)

(root@localhost:3300.socket) [wftest]> insert into t6 values(5,6);
Query OK, 1 row affected (0.00 sec)

(root@localhost:3300.socket) [wftest]> insert into t6 values(7,null);
Query OK, 1 row affected (0.00 sec)

(root@localhost:3300.socket) [wftest]> flush tables;
Query OK, 0 rows affected (0.00 sec)