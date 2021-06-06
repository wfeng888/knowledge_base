  typedef ulonglong HA_ALTER_FLAGS;

  
  static const HA_ALTER_FLAGS ADD_INDEX = 1ULL << 0;  // 添加 非唯一+非主键索引
  static const HA_ALTER_FLAGS DROP_INDEX = 1ULL << 1; // 删除 非唯一+非主键索引
  static const HA_ALTER_FLAGS ADD_UNIQUE_INDEX = 1ULL << 2; // 添加 唯一+非主键索引
  static const HA_ALTER_FLAGS DROP_UNIQUE_INDEX = 1ULL << 3; // 删除 唯一+非主键索引
  static const HA_ALTER_FLAGS ADD_PK_INDEX = 1ULL << 4;  // 添加主键索引
  static const HA_ALTER_FLAGS DROP_PK_INDEX = 1ULL << 5; // 删除主键索引
  // 与Add column相关
  static const HA_ALTER_FLAGS ADD_VIRTUAL_COLUMN = 1ULL << 6; // 添加自产生的虚拟列 
  static const HA_ALTER_FLAGS ADD_STORED_BASE_COLUMN = 1ULL << 7; // 添加普通列
  static const HA_ALTER_FLAGS ADD_STORED_GENERATED_COLUMN = 1ULL << 8; // 存储自产生列Stored generated column
  static const HA_ALTER_FLAGS ADD_COLUMN = 
      ADD_VIRTUAL_COLUMN | ADD_STORED_BASE_COLUMN | ADD_STORED_GENERATED_COLUMN; // 添加子产生列Add generic column (convience constant).
  // 与Drop column相关
  static const HA_ALTER_FLAGS DROP_VIRTUAL_COLUMN = 1ULL << 9;
  static const HA_ALTER_FLAGS DROP_STORED_COLUMN = 1ULL << 10;
  static const HA_ALTER_FLAGS DROP_COLUMN =
      DROP_VIRTUAL_COLUMN | DROP_STORED_COLUMN;
  // 与Rename column相关
  static const HA_ALTER_FLAGS ALTER_COLUMN_NAME = 1ULL << 11;
  static const HA_ALTER_FLAGS ALTER_VIRTUAL_COLUMN_TYPE = 1ULL << 12; // 改变列数据类型
  static const HA_ALTER_FLAGS ALTER_STORED_COLUMN_TYPE = 1ULL << 13;
  /**
    Change column datatype in such way that new type has compatible packed representation with old type, 
    so it is theoretically possible to perform change by only updating data dictionary without changing table rows. 列改变数据类型后的新类型可以与旧类型在存储方式上兼容，因此理论上只需要改变数据字典反应新类型即可
  */
  static const HA_ALTER_FLAGS ALTER_COLUMN_EQUAL_PACK_LENGTH = 1ULL << 14;
  static const HA_ALTER_FLAGS ALTER_VIRTUAL_COLUMN_ORDER = 1ULL << 15; /// 虚拟列改变了位置A virtual column has changed its position
  static const HA_ALTER_FLAGS ALTER_STORED_COLUMN_ORDER = 1ULL << 16; /// 普通列改变了(存储位置)列顺序A stored column has changed its position (disregarding virtual columns)
  static const HA_ALTER_FLAGS ALTER_COLUMN_NULLABLE = 1ULL << 17;  // 修改列 not null 为null，Change column from NOT NULL to NULL
  static const HA_ALTER_FLAGS ALTER_COLUMN_NOT_NULLABLE = 1ULL << 18; //修改列从null到not null, Change column from NULL to NOT NULL
  static const HA_ALTER_FLAGS ALTER_COLUMN_DEFAULT = 1ULL << 19; //设置或删除列默认值 Set or remove default column value
  static const HA_ALTER_FLAGS ALTER_VIRTUAL_GCOL_EXPR = 1ULL << 20; //改变列生成表达式 Change column generation expression
  static const HA_ALTER_FLAGS ALTER_STORED_GCOL_EXPR = 1ULL << 21;
  static const HA_ALTER_FLAGS ADD_FOREIGN_KEY = 1ULL << 22; //添加外键 Add foreign key
  static const HA_ALTER_FLAGS DROP_FOREIGN_KEY = 1ULL << 23; //删除外键 Drop foreign key
  static const HA_ALTER_FLAGS CHANGE_CREATE_OPTION = 1ULL << 24;//修改表创建选项 table_options changed, see HA_CREATE_INFO::used_fields for details.
  static const HA_ALTER_FLAGS ALTER_RENAME = 1ULL << 25; //表重命名 Table is renamed
  static const HA_ALTER_FLAGS ALTER_COLUMN_STORAGE_TYPE = 1ULL << 26; //修改列存储类型 Change the storage type of column
  static const HA_ALTER_FLAGS ALTER_COLUMN_COLUMN_FORMAT = 1ULL << 27; //修改列格式 Change the column format of column
  static const HA_ALTER_FLAGS ADD_PARTITION = 1ULL << 28; // 添加分区 Add partition
  static const HA_ALTER_FLAGS DROP_PARTITION = 1ULL << 29; //删除分区 Drop partition
  static const HA_ALTER_FLAGS ALTER_PARTITION = 1ULL << 30; //修改分区选项 Changing partition options
  static const HA_ALTER_FLAGS COALESCE_PARTITION = 1ULL << 31; //合并分区 Coalesce partition
  static const HA_ALTER_FLAGS REORGANIZE_PARTITION = 1ULL << 32; //拆解分区 Reorganize partition ... into
  static const HA_ALTER_FLAGS ALTER_TABLE_REORG = 1ULL << 33; //拆解分区 Reorganize partition
  static const HA_ALTER_FLAGS ALTER_REMOVE_PARTITIONING = 1ULL << 34; //删除分区 Remove partitioning
  static const HA_ALTER_FLAGS ALTER_ALL_PARTITION = 1ULL << 35; //使用ALL关键字的分区操作 Partition operation with ALL keyword
  /**
    Rename index. Note that we set this flag only if there are no other changes to the index being renamed. Also for simplicity we don't detect renaming of indexes which is done by dropping index and then re-creating index with identical definition under different name.
    仅仅指没有其它附带操作的索引重命名
  */
  static const HA_ALTER_FLAGS RENAME_INDEX = 1ULL << 36;
  /**
    Recreate the table for ALTER TABLE FORCE, ALTER TABLE ENGINE
    and OPTIMIZE TABLE operations.
    alter table force,alter table engine,optimize table等操作
  */
  static const HA_ALTER_FLAGS RECREATE_TABLE = 1ULL << 37;
  static const HA_ALTER_FLAGS ADD_SPATIAL_INDEX = 1ULL << 38; //添加空间索引 Add spatial index
  static const HA_ALTER_FLAGS ALTER_INDEX_COMMENT = 1ULL << 39; //修改索引注释 Alter index comment
  static const HA_ALTER_FLAGS VALIDATE_VIRTUAL_COLUMN = 1ULL << 40; // New/changed virtual generated column require validation
  /**
    Change index option in a way which is likely not to require index
    recreation. For example, change COMMENT or KEY::is_algorithm_explicit
    flag (without change of index algorithm itself).
    不需要重建索引的索引修改操作
  */
  static const HA_ALTER_FLAGS CHANGE_INDEX_OPTION = 1LL << 41;
  static const HA_ALTER_FLAGS ALTER_REBUILD_PARTITION = 1ULL << 42; //分区重建 Rebuild partition
  /**
    Change in index length such that it does not require index rebuild.
    For example, change in index length due to column expansion like
    varchar(X) changed to varchar(X + N).
    不需要重建索引的索引长度修改，例如构成索引的列的长度扩展
  */
  static const HA_ALTER_FLAGS ALTER_COLUMN_INDEX_LENGTH = 1ULL << 43;
  /**
    Change to one of columns on which virtual generated column depends,  so its values require re-evaluation.
    改变虚拟列值产生的依赖普通列，导致虚拟列值需要重新计算
  */
  static const HA_ALTER_FLAGS VIRTUAL_GCOL_REEVAL = 1ULL << 44;
  /**
    Change to one of columns on which stored generated column depends, so its values require re-evaluation.
    改变被存储的子生成列列值产生的依赖普通列，导致虚拟列值需要重新计算
  */
  static const HA_ALTER_FLAGS STORED_GCOL_REEVAL = 1ULL << 45;
  static const HA_ALTER_FLAGS ADD_CHECK_CONSTRAINT = 1ULL << 46; //添加检查约束 Add check constraint. 
  static const HA_ALTER_FLAGS DROP_CHECK_CONSTRAINT = 1ULL << 47; //删除检查约束 Drop check constraint. 
  static const HA_ALTER_FLAGS SUSPEND_CHECK_CONSTRAINT = 1ULL << 48; //暂停检查约束 Suspend check constraint. 
  static const HA_ALTER_FLAGS ALTER_COLUMN_VISIBILITY = 1ULL << 49; //修改列可见性 Alter column visibility.