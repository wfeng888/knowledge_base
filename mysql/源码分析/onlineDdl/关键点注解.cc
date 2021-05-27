sql_table.cc
#15947
const dd::Table *old_table_def = nullptr;


#15959  ,alter_ctx.db、alter_ctx.table_name是"alter table"修改前的表，因此old_table_def是修改前的字典表对象
  if ((table->s->tmp_table == NO_TMP_TABLE) &&
      thd->dd_client()->acquire(alter_ctx.db, alter_ctx.table_name,
                                &old_table_def))
    return true;


# 15920 table，TABLE类型对象，来自table_list的第一个表。
#table_list可以是三种类型，这里作为ddl，应是alter table操作的表的TABLE对象：
/*
1) table (TABLE_LIST::view == NULL)
     - base table
       (TABLE_LIST::derived == NULL)
     - subquery - TABLE_LIST::table is a temp table
       (TABLE_LIST::derived != NULL)
     - information schema table
       (TABLE_LIST::schema_table != NULL)
       NOTICE: for schema tables TABLE_LIST::field_translation may be != NULL
  2) view (TABLE_LIST::view != NULL)
     - merge    (TABLE_LIST::effective_algorithm == VIEW_ALGORITHM_MERGE)
           also (TABLE_LIST::field_translation != NULL)
     - temptable(TABLE_LIST::effective_algorithm == VIEW_ALGORITHM_TEMPTABLE)
           also (TABLE_LIST::field_translation == NULL)
  3) nested table reference (TABLE_LIST::nested_join != NULL)
     - table sequence - e.g. (t1, t2, t3)
       TODO: how to distinguish from a JOIN?
     - general JOIN
       TODO: how to distinguish from a table sequence?
     - NATURAL JOIN
       (TABLE_LIST::natural_join != NULL)
       - JOIN ... USING
         (TABLE_LIST::join_using_fields != NULL)
     - semi-join
       ;
*/
TABLE *table = table_list->table; 

#16335   old_table_def是修改前表的字典表对象
      
  if (mysql_prepare_alter_table(thd, old_table_def, table, create_info,
                                alter_info, &alter_ctx)) {
    return true;
  }






#15946 new_schema是数据字典 Schema 对象，是"alter table" ddl语句的修改后 Schema。
const dd::Schema *new_schema = nullptr;
  if (mdl_locker_1.ensure_locked(alter_ctx.db) ||
      mdl_locker_2.ensure_locked(alter_ctx.new_db) ||
      thd->dd_client()->acquire(alter_ctx.db, &schema) ||
      thd->dd_client()->acquire(alter_ctx.new_db, &new_schema))
    return true;


#16534
#new_schema是数据字典 Schema 对象，是"alter table" ddl语句的修改后 Schema。
#alter_ctx.new_db 是char类型，"alter table" ddl语句的修改后 schema的名称
#alter_ctx.table_name  是char类型，"alter table" ddl语句的修改前的 表的名称
#alter_ctx.tmp_name 是char类型，是  “#sql-f6b_7” 形式的名称
#alter_ctx.get_tmp_path() 是char类型，文件形式 “./wftest/#sql-f6b-7”
#snprintf(tmp_name, sizeof(tmp_name), "%s-%lx_%x", tmp_file_prefix,
#           current_pid, thd->thread_id());
#old_table_def ， 修改前表的dd::Table类型的数据字典对象，
    error = create_table_impl(
        thd, *new_schema, alter_ctx.new_db, alter_ctx.tmp_name,
        alter_ctx.table_name, alter_ctx.get_tmp_path(), create_info, alter_info,
        true, 0, true, true,
        /*
          If target SE supports atomic DDL do not store
          new table version in on-disk DD.
          It is not required to rollback statement in
          case of error and allows to keep correct names
          for pre-existing foreign keys in the dd::Table
          object for new table version.
         */
        (new_db_type->flags & HTON_SUPPORTS_ATOMIC_DDL), nullptr, &key_info,
        &key_count, keys_onoff, &fk_key_info, &fk_key_count, alter_ctx.fk_info,
        alter_ctx.fk_count, old_table_def,
        alter_ctx.fk_max_generated_name_number, &non_dd_table_def, nullptr);







#8665
/*
参数对应来自create_table_impl(。。。)函数参数
db，对应alter_ctx.new_db，是char类型，"alter table" ddl语句的修改前的 schema的名称
error_table_name， 对应alter_ctx.table_name  是char类型，"alter table" ddl语句的修改前的 表的名称
existing_fk_table， 对应old_table_def，修改前表的dd::Table类型的数据字典对象，
*/    
  bool prepare_error = mysql_prepare_create_table(
      thd, db, error_table_name, create_info, alter_info, file.get(),
      (part_info != nullptr), key_info, key_count, fk_key_info, fk_key_count,
      existing_fk_info, existing_fk_count, existing_fk_table,
      fk_max_generated_name_number, select_field_count, find_parent_keys);