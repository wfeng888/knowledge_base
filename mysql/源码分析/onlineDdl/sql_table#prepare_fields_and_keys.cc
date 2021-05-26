// Prepare Create_field and Key_spec objects for ALTER and upgrade.
bool prepare_fields_and_keys(THD *thd, const dd::Table *src_table, TABLE *table,
                             HA_CREATE_INFO *create_info,
                             Alter_info *alter_info, Alter_table_ctx *alter_ctx,
                             const uint &used_fields) {
  /* New column definitions are added here */
  List<Create_field> new_create_list;
  /* New key definitions are added here */
  Mem_root_array<Key_spec *> new_key_list(thd->mem_root);

  /*
    Original Alter_info::drop_list is used by foreign key handling code and
    storage engines. check_if_field_used_by_generated_column_or_default()
    also needs original Alter_info::drop_list. So this function should not
    modify original list but rather work with its copy.
  */
  Prealloced_array<const Alter_drop *, 1> drop_list(
      PSI_INSTRUMENT_ME, alter_info->drop_list.cbegin(),
      alter_info->drop_list.cend());

  /*
    Alter_info::alter_rename_key_list is also used by fill_alter_inplace_info()
    call. So this function should not modify original list but rather work with
    its copy.
  */
  Prealloced_array<const Alter_rename_key *, 1> rename_key_list(
      PSI_INSTRUMENT_ME, alter_info->alter_rename_key_list.cbegin(),
      alter_info->alter_rename_key_list.cend());

  /*
    This is how we check that all indexes to be altered are name-resolved: We
    make a copy of the list from the alter_info, and remove all the indexes
    that are found in the table. Later we check that there is nothing left in
    the list. This is obviously just a copy-paste of what is done for renamed
    indexes.
  */
  Prealloced_array<const Alter_index_visibility *, 1> index_visibility_list(
      PSI_INSTRUMENT_ME, alter_info->alter_index_visibility_list.cbegin(),
      alter_info->alter_index_visibility_list.cend());

  /*
    Alter_info::alter_list is used by fill_alter_inplace_info() call as well.
    So this function works on its copy rather than original list.
  */
  Prealloced_array<const Alter_column *, 1> alter_list(
      PSI_INSTRUMENT_ME, alter_info->alter_list.cbegin(),
      alter_info->alter_list.cend());

  List_iterator<Create_field> def_it(alter_info->create_list);
  List_iterator<Create_field> find_it(new_create_list);
  List_iterator<Create_field> field_it(new_create_list);
  List<Key_part_spec> key_parts;
  KEY *key_info = table->key_info;

  DBUG_TRACE;

  /*
    During upgrade from 5.7, old tables are temporarily accessed to
    get the keys and fields, and in this process, we assign
    table->record[0] = table->s->default_values, hence, we make the
    call to restore_record() below conditional to avoid valgrind errors
    due to overlapping source and destination for memcpy.
  */
  if (table->record[0] != table->s->default_values)
    restore_record(table, s->default_values);  // Empty record for DEFAULT

  std::vector<Create_field *> functional_index_columns;
  Create_field *def;

  /*
    First collect all fields from table which isn't in drop_list
  */
  /*
  对旧表的每一个字段，到drop_list中找一下：
        如果在drop_list中找到了，做一下删除校验，并将此字段从drop_list中删除；
        如果在drop_list中没有找到，则到create_list中找一下，
  */
  Field **f_ptr, *field;
  for (f_ptr = table->field; (field = *f_ptr); f_ptr++) {
    /* Check if field should be dropped */
    size_t i = 0;
    while (i < drop_list.size()) {
      const Alter_drop *drop = drop_list[i];
      if (drop->type == Alter_drop::COLUMN &&
          !my_strcasecmp(system_charset_info, field->field_name, drop->name)) {
        /* Reset auto_increment value if it was dropped */
        /*
        如果旧表的自增字段在drop列表中(将要被删除)，同时创建信息中没有标记使用自增字段的，在创建信息中加上使用自增字段的标识
        (先创建再修改？)。同时，既然都要删除了，那当前的自增的值也不重要了，直接置为0
        */
        if ((field->auto_flags & Field::NEXT_NUMBER) &&
            !(used_fields & HA_CREATE_USED_AUTO)) {
          create_info->auto_increment_value = 0;
          create_info->used_fields |= HA_CREATE_USED_AUTO;
        }

        /*
          If a generated column or a default expression is dependent
          on this column, this column cannot be dropped.

          The same applies to case when this table is partitioned and
          we drop column used by partitioning function.
        */
        /*
        如果在drop_list中找到了，做一下删除校验，并将此字段从drop_list中删除；
        */
        if (check_if_field_used_by_generated_column_or_default(table, field,
                                                               alter_info) ||
            check_if_field_used_by_partitioning_func(table, field, alter_info))
          return true;

        break;  // Column was found.
      }
      i++;
    }
    /*
    如果在drop_list中找到了，做一下删除校验，并将此字段从drop_list中删除；
    */
    if (i < drop_list.size()) {
      drop_list.erase(i);
      continue;
    }
    /* Check if field is changed */
    def_it.rewind();
    /*
    如果在drop_list中没有找到，则到create_list中(根据此字段被alter table change|modify子句使用)找一下，找到了，使用原表值填充def->field、def->charset，并做一些校验
    并且将在create_list中找到的，塞到新的new_create_list列表中
    */
    while ((def = def_it++)) {
      if (def->change &&
          !my_strcasecmp(system_charset_info, field->field_name, def->change))
        break;
    }
    if (def) {  // Field is changed
      def->field = field;
      def->charset = get_sql_field_charset(def, create_info);

      if (field->stored_in_db != def->stored_in_db) {
        my_error(ER_UNSUPPORTED_ACTION_ON_GENERATED_COLUMN, MYF(0),
                 "Changing the STORED status");
        return true;
      }

      /*
        If a generated column or a default expression is dependent
        on this column, this column cannot be renamed.

        The same applies to case when this table is partitioned and
        we rename column used by partitioning function.
      */
      if ((my_strcasecmp(system_charset_info, def->field_name, def->change) !=
           0) &&
          (check_if_field_used_by_generated_column_or_default(table, field,
                                                              alter_info) ||
           check_if_field_used_by_partitioning_func(table, field, alter_info)))
        return true;

      /*
        Add column being updated to the list of new columns.
        Note that columns with AFTER clauses are added to the end
        of the list for now. Their positions will be corrected later.
      */
      new_create_list.push_back(def);

      /*
        If the new column type is GEOMETRY (or a subtype) NOT NULL,
        and the old column type is nullable and not GEOMETRY (or a
        subtype), existing NULL values will be converted into empty
        strings in non-strict mode. Empty strings are illegal values
        in GEOMETRY columns.

        However, generated columns have implicit default values, so they can be
        NOT NULL.
      */
      if (def->sql_type == MYSQL_TYPE_GEOMETRY &&
          (def->flags & (NO_DEFAULT_VALUE_FLAG | NOT_NULL_FLAG)) &&
          field->type() != MYSQL_TYPE_GEOMETRY && field->is_nullable() &&
          !thd->is_strict_mode() && !def->is_gcol()) {
        alter_ctx->error_if_not_empty |=
            Alter_table_ctx::GEOMETRY_WITHOUT_DEFAULT;
      }
    } else {
        /*
        如果既没有在drop_list中找到，也没有在change(没有被修改定义)中找到，说明这个field字段没有被修改，在新建的表中保持原样
        */
      /*
        This field was not dropped and the definition is not changed, add
        it to the list for the new table.
      */
    /*
    构造一个结构，如果这个字段出现在80018后的版本的函数索引中(这个版本引入了新特性，将函数索引的最后一个字段进行排序?)，
        在后面将这个结构插入到新建的functional_index_columns中并且在后面将functional_index_columns列表中的字段重新排序后插入new_create_list
        ，否则直接插入到 new_create_list。 new_create_list保存新表的全部列字段信息
        */
      def = new (thd->mem_root) Create_field(field, field);

      // Mark if collation was specified explicitly by user for the column.
      const dd::Table *obj =
          (table->s->tmp_table ? table->s->tmp_table_def : src_table);
      // In case of upgrade, we do not have src_table.
      if (!obj)
        def->is_explicit_collation = false;
      else
        def->is_explicit_collation =
            obj->get_column(field->field_name)->is_explicit_collation();

      // If we have a replication setup _and_ the master doesn't sort
      // functional index columns last in the table, we will not do it either.
      // Otherwise, we will position the functional index columns last in the
      // table, sorted on their name.
      if (is_field_for_functional_index(def) &&
          is_not_slave_or_master_sorts_functional_index_columns_last(
              thd->variables.original_server_version)) {
        functional_index_columns.push_back(def);
      } else {
        new_create_list.push_back(def);
      }

      if (alter_column_name_default_or_visibility(alter_info, &alter_list, def))
        return true;
    }
  }
  def_it.rewind();
  while ((def = def_it++))  // Add new columns
  {
    if (def->change && !def->field) {
      my_error(ER_BAD_FIELD_ERROR, MYF(0), def->change,
               table->s->table_name.str);
      return true;
    }

    warn_on_deprecated_float_auto_increment(thd, *def);

    /*
      If this ALTER TABLE doesn't have an AFTER clause for the modified
      column then it doesn't need further processing.
    */
    if (def->change && !def->after) continue;

    /*
      New columns of type DATE/DATETIME/GEOMETRIC with NOT NULL constraint
      added as part of ALTER operation will generate zero date for DATE/
      DATETIME types and empty string for GEOMETRIC types when the table
      is not empty. Hence certain additional checks needs to be performed
      as described below. This cannot be caught by SE(For INPLACE ALTER)
      since it checks for only NULL value. Zero date and empty string
      does not violate the NOT NULL value constraint.
    */
    if (!def->change) {
      /*
        Check that the DATE/DATETIME NOT NULL field we are going to
        add either has a default value, is a generated column, or the
        date '0000-00-00' is allowed by the set sql mode.

        If the '0000-00-00' value isn't allowed then raise the
        error_if_not_empty flag to allow ALTER TABLE only if the table to be
        altered is empty.
      */
      if ((def->sql_type == MYSQL_TYPE_DATE ||
           def->sql_type == MYSQL_TYPE_NEWDATE ||
           def->sql_type == MYSQL_TYPE_DATETIME ||
           def->sql_type == MYSQL_TYPE_DATETIME2) &&
          !alter_ctx->datetime_field && !def->is_gcol() &&
          !(~def->flags & (NO_DEFAULT_VALUE_FLAG | NOT_NULL_FLAG))) {
        alter_ctx->datetime_field = def;
        alter_ctx->error_if_not_empty |=
            Alter_table_ctx::DATETIME_WITHOUT_DEFAULT;
      }

      /*
        New GEOMETRY (and subtypes) columns can't be NOT NULL unless they have a
        default value. Explicit default values are currently not supported for
        geometry columns. To add a GEOMETRY NOT NULL column, first create a
        GEOMETRY NULL column, UPDATE the table to set a different value than
        NULL, and then do a ALTER TABLE MODIFY COLUMN to set NOT NULL.

        This restriction can be lifted once MySQL supports explicit default
        values (i.e., functions) for geometry columns. The new restriction would
        then be for added GEOMETRY NOT NULL columns to always have a provided
        default value.

        Generated columns (including generated geometry columns) have implicit
        default values, so they can be NOT NULL.
      */
      if (def->sql_type == MYSQL_TYPE_GEOMETRY && !def->is_gcol() &&
          (def->flags & (NO_DEFAULT_VALUE_FLAG | NOT_NULL_FLAG))) {
        alter_ctx->error_if_not_empty |=
            Alter_table_ctx::GEOMETRY_WITHOUT_DEFAULT;
      }
    }

    if (!def->after)
      new_create_list.push_back(def);
    else {
      const Create_field *find;
      if (def->change) {
        find_it.rewind();
        /*
          For columns being modified with AFTER clause we should first remove
          these columns from the list and then add them back at their correct
          positions.
        */
        while ((find = find_it++)) {
          /*
            Create_fields representing changed columns are added directly
            from Alter_info::create_list to new_create_list. We can therefore
            safely use pointer equality rather than name matching here.
            This prevents removing the wrong column in case of column rename.
          */
          if (find == def) {
            find_it.remove();
            break;
          }
        }
      }
      if (def->after == first_keyword)
        new_create_list.push_front(def);
      else {
        find_it.rewind();
        while ((find = find_it++)) {
          if (!my_strcasecmp(system_charset_info, def->after, find->field_name))
            break;
        }
        if (!find) {
          my_error(ER_BAD_FIELD_ERROR, MYF(0), def->after,
                   table->s->table_name.str);
          return true;
        }
        find_it.after(def);  // Put column after this
      }
    }
  }
  if (alter_list.size() > 0) {
    my_error(ER_BAD_FIELD_ERROR, MYF(0), alter_list[0]->name,
             table->s->table_name.str);
    return true;
  }

  // Ensure that hidden generated column for functional indexes are inserted at
  // the end, sorted by their column name.
  std::sort(functional_index_columns.begin(), functional_index_columns.end(),
            [](const Create_field *a, const Create_field *b) {
              return my_strcasecmp(system_charset_info, a->field_name,
                                   b->field_name) < 0;
            });

  for (Create_field *ic_field : functional_index_columns) {
    new_create_list.push_back(ic_field);
  }

  if (!new_create_list.elements) {
    my_error(ER_CANT_REMOVE_ALL_FIELDS, MYF(0));
    return true;
  }

  /*
    Collect all keys which isn't in drop list. Add only those
    for which some fields exists.
  */

  for (uint i = 0; i < table->s->keys; i++, key_info++) {
    const char *key_name = key_info->name;
    bool index_column_dropped = false;
    size_t drop_idx = 0;
    while (drop_idx < drop_list.size()) {
      const Alter_drop *drop = drop_list[drop_idx];
      if (drop->type == Alter_drop::KEY &&
          !my_strcasecmp(system_charset_info, key_name, drop->name))
        break;
      drop_idx++;
    }
    if (drop_idx < drop_list.size()) {
      drop_list.erase(drop_idx);
      continue;
    }

    KEY_PART_INFO *key_part = key_info->key_part;
    key_parts.clear();
    for (uint j = 0; j < key_info->user_defined_key_parts; j++, key_part++) {
      if (!key_part->field) continue;  // Wrong field (from UNIREG)
      const char *key_part_name = key_part->field->field_name;
      const Create_field *cfield;
      field_it.rewind();
      while ((cfield = field_it++)) {
        if (cfield->change) {
          if (!my_strcasecmp(system_charset_info, key_part_name,
                             cfield->change))
            break;
        } else if (!my_strcasecmp(system_charset_info, key_part_name,
                                  cfield->field_name))
          break;
      }
      if (!cfield) {
        /*
           We are dropping a column associated with an index.
        */
        index_column_dropped = true;
        continue;  // Field is removed
      }
      uint key_part_length = key_part->length;
      if (cfield->field)  // Not new field
      {
        /*
          If the field can't have only a part used in a key according to its
          new type, or should not be used partially according to its
          previous type, or the field length is less than the key part
          length, unset the key part length.

          We also unset the key part length if it is the same as the
          old field's length, so the whole new field will be used.

          BLOBs may have cfield->length == 0, which is why we test it before
          checking whether cfield->length < key_part_length (in chars).

          In case of TEXTs we check the data type maximum length *in bytes*
          to key part length measured *in characters* (i.e. key_part_length
          devided to mbmaxlen). This is because it's OK to have:
          CREATE TABLE t1 (a tinytext, key(a(254)) character set utf8);
          In case of this example:
          - data type maximum length is 255.
          - key_part_length is 1016 (=254*4, where 4 is mbmaxlen)
         */
        if (!Field::type_can_have_key_part(cfield->field->type()) ||
            !Field::type_can_have_key_part(cfield->sql_type) ||
            /* spatial keys can't have sub-key length */
            (key_info->flags & HA_SPATIAL) ||
            (cfield->field->field_length == key_part_length &&
             key_part->field->type() != MYSQL_TYPE_BLOB) ||
            (cfield->max_display_width_in_codepoints() &&
             (((cfield->sql_type >= MYSQL_TYPE_TINY_BLOB &&
                cfield->sql_type <= MYSQL_TYPE_BLOB)
                   ? blob_length_by_type(cfield->sql_type)
                   : cfield->max_display_width_in_codepoints()) <
              key_part_length / key_part->field->charset()->mbmaxlen)))
          key_part_length = 0;  // Use whole field
      }
      key_part_length /= key_part->field->charset()->mbmaxlen;
      // The Key_part_spec constructor differentiates between explicit ascending
      // (ORDER_ASC) and implicit ascending order (ORDER_NOT_RELEVANT). However,
      // here we only have HA_REVERSE_SORT to base our ordering decision on. The
      // only known case where the difference matters is in case of indexes on
      // geometry columns and typed arrays, which can't have explicit ordering.
      // Therefore, in such cases we pass ORDER_NOT_RELEVANT.
      enum_order order =
          key_part->key_part_flag & HA_REVERSE_SORT
              ? ORDER_DESC
              : ((key_part->field->type() == MYSQL_TYPE_GEOMETRY ||
                  key_part->field->is_array())
                     ? ORDER_NOT_RELEVANT
                     : ORDER_ASC);
      if (key_part->field->is_field_for_functional_index()) {
        key_parts.push_back(new (thd->mem_root) Key_part_spec(
            cfield->field_name, key_part->field->gcol_info->expr_item, order));
      } else {
        key_parts.push_back(new (thd->mem_root) Key_part_spec(
            to_lex_cstring(cfield->field_name), key_part_length, order));
      }
    }
    if (key_parts.elements) {
      KEY_CREATE_INFO key_create_info(key_info->is_visible);

      keytype key_type;

      /* If this index is to stay in the table check if it has to be renamed. */
      for (size_t rename_idx = 0; rename_idx < rename_key_list.size();
           rename_idx++) {
        const Alter_rename_key *rename_key = rename_key_list[rename_idx];
        if (!my_strcasecmp(system_charset_info, key_name,
                           rename_key->old_name)) {
          if (!my_strcasecmp(system_charset_info, key_name, primary_key_name)) {
            my_error(ER_WRONG_NAME_FOR_INDEX, MYF(0), rename_key->old_name);
            return true;
          } else if (!my_strcasecmp(system_charset_info, rename_key->new_name,
                                    primary_key_name)) {
            my_error(ER_WRONG_NAME_FOR_INDEX, MYF(0), rename_key->new_name);
            return true;
          }

          key_name = rename_key->new_name;
          rename_key_list.erase(rename_idx);
          /*
            If the user has explicitly renamed the key, we should no longer
            treat it as generated. Otherwise this key might be automatically
            dropped by mysql_prepare_create_table() and this will confuse
            code in fill_alter_inplace_info().
          */
          key_info->flags &= ~HA_GENERATED_KEY;
          break;
        }
      }

      // Erase all alter operations that operate on this index.
      for (auto it = index_visibility_list.begin();
           it < index_visibility_list.end();)
        if (my_strcasecmp(system_charset_info, key_name, (*it)->name()) == 0)
          index_visibility_list.erase(it);
        else
          ++it;

      if (key_info->is_algorithm_explicit) {
        key_create_info.algorithm = key_info->algorithm;
        key_create_info.is_algorithm_explicit = true;
      } else {
        /*
          If key algorithm was not specified explicitly for source table
          don't specify one a new version as well, This allows to handle
          ALTER TABLEs which change SE nicely.
          OTOH this means that any ALTER TABLE will rebuild such keys when
          SE changes default algorithm for key. Code will have to be adjusted
          to handle such situation more gracefully.
        */
        DBUG_ASSERT((key_create_info.is_algorithm_explicit == false) &&
                    (key_create_info.algorithm == HA_KEY_ALG_SE_SPECIFIC));
      }

      if (key_info->flags & HA_USES_BLOCK_SIZE)
        key_create_info.block_size = key_info->block_size;
      if (key_info->flags & HA_USES_PARSER)
        key_create_info.parser_name = *plugin_name(key_info->parser);
      if (key_info->flags & HA_USES_COMMENT)
        key_create_info.comment = key_info->comment;

      if (key_info->engine_attribute.str != nullptr)
        key_create_info.m_engine_attribute = key_info->engine_attribute;

      if (key_info->secondary_engine_attribute.str != nullptr)
        key_create_info.m_secondary_engine_attribute =
            key_info->secondary_engine_attribute;

      for (const Alter_index_visibility *alter_index_visibility :
           alter_info->alter_index_visibility_list) {
        const char *name = alter_index_visibility->name();
        if (my_strcasecmp(system_charset_info, key_name, name) == 0) {
          if (table->s->primary_key <= MAX_KEY &&
              table->key_info + table->s->primary_key == key_info) {
            my_error(ER_PK_INDEX_CANT_BE_INVISIBLE, MYF(0));
            return true;
          }
          key_create_info.is_visible = alter_index_visibility->is_visible();
        }
      }

      if (key_info->flags & HA_SPATIAL)
        key_type = KEYTYPE_SPATIAL;
      else if (key_info->flags & HA_NOSAME) {
        if (!my_strcasecmp(system_charset_info, key_name, primary_key_name))
          key_type = KEYTYPE_PRIMARY;
        else
          key_type = KEYTYPE_UNIQUE;
      } else if (key_info->flags & HA_FULLTEXT)
        key_type = KEYTYPE_FULLTEXT;
      else
        key_type = KEYTYPE_MULTIPLE;

      /*
        If we have dropped a column associated with an index,
        this warrants a check for duplicate indexes
      */
      new_key_list.push_back(new (thd->mem_root) Key_spec(
          thd->mem_root, key_type, to_lex_cstring(key_name), &key_create_info,
          (key_info->flags & HA_GENERATED_KEY), index_column_dropped,
          key_parts));
    }
  }
  {
    new_key_list.reserve(new_key_list.size() + alter_info->key_list.size());
    for (size_t i = 0; i < alter_info->key_list.size(); i++)
      new_key_list.push_back(alter_info->key_list[i]);  // Add new keys
  }

  /*
    Copy existing foreign keys from the source table into
    Alter_table_ctx so that they can be added to the new table
    later. Omits foreign keys to be dropped and removes them
    from the drop_list. Checks that foreign keys to be kept
    are still valid.
  */
  if (create_info->db_type->flags & HTON_SUPPORTS_FOREIGN_KEYS) {
    if (transfer_preexisting_foreign_keys(
            thd, src_table, table->s->db.str, table->s->table_name.str,
            table->s->db_type(), alter_info, &new_create_list, alter_ctx,
            &drop_list))
      return true;
  }

  if (drop_list.size() > 0) {
    // Now this contains only DROP for not-found objects.
    for (const Alter_drop *drop : drop_list) {
      switch (drop->type) {
        case Alter_drop::FOREIGN_KEY:
          if (!(create_info->db_type->flags & HTON_SUPPORTS_FOREIGN_KEYS)) {
            /*
              For historical reasons we silently ignore attempts to drop
              foreign keys from tables in storage engines which don't
              support them. This is in sync with the fact that attempts
              to add foreign keys to such tables are silently ignored
              as well. Once the latter is changed the former hack can
              be removed as well.
            */
            break;
          }
          // Fall-through.
        case Alter_drop::KEY:
        case Alter_drop::COLUMN:
          my_error(ER_CANT_DROP_FIELD_OR_KEY, MYF(0), drop->name);
          return true;
        case Alter_drop::CHECK_CONSTRAINT:
          /*
            Check constraints to be dropped are already handled by the
            prepare_check_constraints_for_alter().
          */
          break;
        case Alter_drop::ANY_CONSTRAINT:
          /*
            Constraint type is resolved by name and a new Alter_drop element
            with resolved type is added to the Alter_drop list.
            Alter_drop::ANY_CONSTRAINT element is retained in the Alter_drop
            list to support re-execution of stored routine or prepared
            statement.
          */
          break;
        default:
          DBUG_ASSERT(false);
          break;
      }
    }
  }

  if (rename_key_list.size() > 0) {
    my_error(ER_KEY_DOES_NOT_EXITS, MYF(0), rename_key_list[0]->old_name,
             table->s->table_name.str);
    return true;
  }
  if (index_visibility_list.size() > 0) {
    my_error(ER_KEY_DOES_NOT_EXITS, MYF(0), index_visibility_list[0]->name(),
             table->s->table_name.str);
    return true;
  }
  /*
  新表的所有信息最终都汇聚到alter_info的create_list和key_list以及其它字段中
  */
  alter_info->create_list.swap(new_create_list);
  alter_info->key_list.clear();
  alter_info->key_list.resize(new_key_list.size());
  std::copy(new_key_list.begin(), new_key_list.end(),
            alter_info->key_list.begin());

  return false;
}