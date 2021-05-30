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
/*
# new_schema是数据字典 Schema 对象，是"alter table" ddl语句的修改后 Schema。
# alter_ctx.new_db 是char类型，"alter table" ddl语句的修改后 schema的名称
# alter_ctx.table_name  是char类型，"alter table" ddl语句的修改前的 表的名称
# alter_ctx.tmp_name 是char类型，是  “#sql-f6b_7” 形式的名称
# alter_ctx.get_tmp_path() 是char类型，文件形式 “./wftest/#sql-f6b-7”
# snprintf(tmp_name, sizeof(tmp_name), "%s-%lx_%x", tmp_file_prefix,
#           current_pid, thd->thread_id());
# new_schema，对应thd->dd_client()->acquire(alter_ctx.new_db, &new_schema)，是ddl修改后schema的数据字典dd:Schema类型对象
# old_table_def ， 修改前表的表(alter table 操作的对象) dd::Table类型的数据字典对象，
# non_dd_table_def, 就是此次创建出来的dd:Table类型的对象
# no_ha_table==true，表明 “#sql-f6b_7”只创建了数据字典对象(server层面)，没有在存储引擎层面创建表空间文件
# do_not_store_in_dd==true，表明“#sql-f6b_7”只是创建了数据字典对象，没有保存以及提交到数据字典缓存中(对象只在内存中存在，没有落实到数据字典文件中)
*/
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
db，对应alter_ctx.new_db，是char类型，"alter table" ddl语句的修改后的 schema的名称
error_table_name， 对应alter_ctx.table_name  是char类型，"alter table" ddl语句的修改前的 表的名称
existing_fk_table， 对应old_table_def，修改前表的dd::Table类型的数据字典对象，
file.get()，是一个新建的ha_innobase对象，不代表任何表
*/    
  bool prepare_error = mysql_prepare_create_table(
      thd, db, error_table_name, create_info, alter_info, file.get(),
      (part_info != nullptr), key_info, key_count, fk_key_info, fk_key_count,
      existing_fk_info, existing_fk_count, existing_fk_table,
      fk_max_generated_name_number, select_field_count, find_parent_keys);






#8739
  /*
#path，对应alter_ctx.get_tmp_path() 是char类型，文件形式 “./wftest/#sql-f6b-7”
#schema，对应thd->dd_client()->acquire(alter_ctx.new_db, &new_schema)中new_schema，是ddl修改后schema的数据字典dd:Schema类型对象
#table_name，对应alter_ctx.tmp_name，是char类型，是  “#sql-f6b_7” 形式的名称
#table_def，对应non_dd_table_def，就是本次创建的dd:Talbe类型的对象
#db, 对应alter_ctx.new_db 是char类型，"alter table" ddl语句的修改后 schema的名称
#do_not_store_in_dd，表示推迟将创建出来的数据字典表定义存储到数据字典中，少了thd->dd_client()->store(table_def_res.get())步骤
#no_ha_table==true，只创建数据字典定义，而不创建存储引擎表，相比no_ha_table==false，少了
    ha_create_table(thd, path, db, table_name, create_info, false, false,
                          table_def)
    方法调用，因此不会创建表空间文件

  */
/*
因为create_info->tablespace没有 提供|设置 表空间名称，因此这个过程只创建了数据字典定义，没有创建对应的表空间
*/
rea_create_base_table(thd, path, schema, db, table_name, create_info,
                              alter_info->create_list, *key_count, *key_info,
                              keys_onoff, *fk_key_count, *fk_key_info,
                              &alter_info->check_constraint_spec_list,
                              file.get(), no_ha_table, do_not_store_in_dd,
                              part_info, is_trans, table_def, post_ddl_ht)






#16779, 从已经创建的dd::Table构造一个TABLE类型的对象
/*
alter_ctx.get_tmp_path() 是char类型，文件形式 “./wftest/#sql-f6b-7”
alter_ctx.new_db char类型，ddl修改后的db名称
alter_ctx.tmp_name，是char类型，是  “#sql-f6b_7” 形式的名称
table_def, 前面create_table_impl步骤创建的，dd::Table *table_def = non_dd_table_def.get()
*/
altered_table = open_table_uncached(
              thd, alter_ctx.get_tmp_path(), alter_ctx.new_db,
              alter_ctx.tmp_name, true, false, *table_def)


#sql_base.cc 7037 
/*
path,上面的alter_ctx.get_tmp_path()
db，上面的alter_ctx.new_db
table_name, 上面的alter_ctx.tmp_name
add_to_temporary_tables_list, true
open_in_engine, false
*/
TABLE *open_table_uncached(THD *thd, const char *path, const char *db,
                           const char *table_name,
                           bool add_to_temporary_tables_list,
                           bool open_in_engine, const dd::Table &table_def) 





#16910
/*
schema, ddl修改前的dd:Schema类型对象 thd->dd_client()->acquire(alter_ctx.db, &schema) 
#new_schema, ddl修改后schema的数据字典dd:Schema类型对象。 对应thd->dd_client()->acquire(alter_ctx.new_db, &new_schema)中new_schema，
old_table_def, ddl前的表的dd::Table类型对象 thd->dd_client()->acquire(alter_ctx.db, alter_ctx.table_name,&old_table_def)
table_def, create_table_impl创建的“wftest.#sql-f6b-7”表的dd::Table数据字典对象 对应dd::Table *table_def = non_dd_table_def.get()
table_list, ddl变更前的表引用列表TABLE_LIST
table, TABLE *table = table_list->table;
altered_table, 创建wftest.#sql-f6b-7出来的TABLE类型对象
ha_alter_info, Alter_inplace_info类型
alter_ctx, Alter_table_ctx类型

*/
mysql_inplace_alter_table(thd, *schema, *new_schema, old_table_def,
                                    table_def, table_list, table, altered_table,
                                    &ha_alter_info, inplace_supported,
                                    &alter_ctx, columns, fk_key_info,
                                    fk_key_count, &fk_invalidator)
/*
table_def, 对应上面的old_table_def ,对应ddl 旧表
altered_table_def, 对应上面的 table_def, “wftest.#sql-f6b-7”表
*/
mysql_inplace_alter_table(
    THD *thd, const dd::Schema &schema, const dd::Schema &new_schema,
    const dd::Table *table_def, dd::Table *altered_table_def,
    TABLE_LIST *table_list, TABLE *table, TABLE *altered_table,
    Alter_inplace_info *ha_alter_info,
    enum_alter_inplace_result inplace_supported, Alter_table_ctx *alter_ctx,
    histograms::columns_set &columns, FOREIGN_KEY *fk_key_info,
    uint fk_key_count, Foreign_key_parents_invalidator *fk_invalidator)




"{原数据库名称}/#sql-ib{原表id,所有innodb表唯一}-{临时文件号}"
"wftest/#sql-ib1065-3591498112"