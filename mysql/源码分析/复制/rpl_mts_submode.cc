/* Copyright (c) 2013, 2020, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "sql/rpl_mts_submode.h"

#include <limits.h>
#include <string.h>
#include <time.h>
#include <memory>

#include "lex_string.h"
#include "libbinlogevents/include/compression/iterator.h"
#include "m_string.h"
#include "my_byteorder.h"
#include "my_compiler.h"
#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_loglevel.h"
#include "my_systime.h"
#include "my_thread.h"
#include "mysql/components/services/log_builtins.h"
#include "mysql/components/services/psi_stage_bits.h"
#include "mysql/psi/mysql_cond.h"
#include "mysql/psi/mysql_mutex.h"
#include "mysqld_error.h"
#include "sql/binlog_reader.h"
#include "sql/debug_sync.h"
#include "sql/log.h"
#include "sql/log_event.h"  // Query_log_event
#include "sql/mdl.h"
#include "sql/mysqld.h"  // stage_worker_....
#include "sql/query_options.h"
#include "sql/rpl_filter.h"
#include "sql/rpl_rli.h"      // Relay_log_info
#include "sql/rpl_rli_pdb.h"  // db_worker_hash_entry
#include "sql/rpl_slave.h"
#include "sql/rpl_slave_commit_order_manager.h"  // Commit_order_manager
#include "sql/sql_class.h"                       // THD
#include "sql/system_variables.h"
#include "sql/table.h"

/**
 Does necessary arrangement before scheduling next event.
 @return 1  if  error
          0 no error
*/
int Mts_submode_database::schedule_next_event(Relay_log_info *, Log_event *) {
  /*nothing to do here*/
  return 0;
}

/**
  Logic to attach temporary tables.
*/
void Mts_submode_database::attach_temp_tables(THD *thd, const Relay_log_info *,
                                              Query_log_event *ev) {
  int i, parts;
  DBUG_TRACE;
  if (!is_mts_worker(thd) || (ev->ends_group() || ev->starts_group())) return;
  DBUG_ASSERT(!thd->temporary_tables);
  // in over max-db:s case just one special partition is locked
  parts = ((ev->mts_accessed_dbs == OVER_MAX_DBS_IN_EVENT_MTS)
               ? 1
               : ev->mts_accessed_dbs);
  for (i = 0; i < parts; i++) {
    mts_move_temp_tables_to_thd(
        thd, ev->mts_assigned_partitions[i]->temporary_tables);
    ev->mts_assigned_partitions[i]->temporary_tables = nullptr;
  }
}

/**
   Function is called by Coordinator when it identified an event
   requiring sequential execution.
   Creating sequential context for the event includes waiting
   for the assigned to Workers tasks to be completed and their
   resources such as temporary tables be returned to Coordinator's
   repository.
   In case all workers are waited Coordinator changes its group status.

   @param  rli     Relay_log_info instance of Coordinator
   @param  ignore  Optional Worker instance pointer if the sequential context
                   is established due for the ignore Worker. Its resources
                   are to be retained.

   @note   Resources that are not occupied by Workers such as
           a list of temporary tables held in unused (zero-usage) records
           of APH are relocated to the Coordinator placeholder.

   @return non-negative number of released by Workers partitions
           (one partition by one Worker can count multiple times)

           or -1 to indicate there has been a failure on a not-ignored Worker
           as indicated by its running_status so synchronization can't succeed.
*/

int Mts_submode_database::wait_for_workers_to_finish(Relay_log_info *rli,
                                                     Slave_worker *ignore) {
  uint ret = 0;
  THD *thd = rli->info_thd;
  bool cant_sync = false;
  char llbuf[22];

  DBUG_TRACE;

  llstr(rli->get_event_relay_log_pos(), llbuf);
  DBUG_PRINT("info", ("Coordinator and workers enter synchronization "
                      "procedure when scheduling event relay-log: %s "
                      "pos: %s",
                      rli->get_event_relay_log_name(), llbuf));

  mysql_mutex_lock(&rli->slave_worker_hash_lock);

  for (const auto &key_and_value : rli->mapping_db_to_worker) {
    db_worker_hash_entry *entry = key_and_value.second.get();
    DBUG_ASSERT(entry);

    // the ignore Worker retains its active resources
    if (ignore && entry->worker == ignore && entry->usage > 0) {
      continue;
    }

    if (entry->usage > 0 && !thd->killed) {
      PSI_stage_info old_stage;
      Slave_worker *w_entry = entry->worker;

      entry->worker = nullptr;  // mark Worker to signal when  usage drops to 0
      thd->ENTER_COND(
          &rli->slave_worker_hash_cond, &rli->slave_worker_hash_lock,
          &stage_slave_waiting_worker_to_release_partition, &old_stage);
      do {
        mysql_cond_wait(&rli->slave_worker_hash_cond,
                        &rli->slave_worker_hash_lock);
        DBUG_PRINT("info", ("Either got awakened of notified: "
                            "entry %p, usage %lu, worker %lu",
                            entry, entry->usage, w_entry->id));
      } while (entry->usage != 0 && !thd->killed);
      entry->worker =
          w_entry;  // restoring last association, needed only for assert
      mysql_mutex_unlock(&rli->slave_worker_hash_lock);
      thd->EXIT_COND(&old_stage);
      ret++;
    } else {
      mysql_mutex_unlock(&rli->slave_worker_hash_lock);
    }
    // resources relocation
    mts_move_temp_tables_to_thd(thd, entry->temporary_tables);
    entry->temporary_tables = nullptr;
    if (entry->worker->running_status != Slave_worker::RUNNING)
      cant_sync = true;
    mysql_mutex_lock(&rli->slave_worker_hash_lock);
  }

  mysql_mutex_unlock(&rli->slave_worker_hash_lock);

  if (!ignore) {
    DBUG_PRINT("info", ("Coordinator synchronized with workers, "
                        "waited entries: %d, cant_sync: %d",
                        ret, cant_sync));

    rli->mts_group_status = Relay_log_info::MTS_NOT_IN_GROUP;
  }

  return !cant_sync ? ret : -1;
}

bool Mts_submode_database::unfold_transaction_payload_event(
    Format_description_event &fde, Transaction_payload_log_event &tple,
    std::vector<Log_event *> &events) {
  bool error = false;
  /*
    disable checksums - there are no checksums for events inside the tple
    otherwise, the last 4 bytes would be truncated.

    We do this by copying the fdle from the rli. Then we disable the checksum
    in the copy. Then we use it to decode the events in the payload instead
    of the original fdle.

    We allocate the fdle copy in the stack.

    TODO: simplify this by breaking the binlog_event_deserialize API
    and make it take a single boolean instead that states whether the
    event has a checksum in it or not.
  */
  Format_description_log_event fdle(fde.reader().buffer(), &fde);
  fdle.footer()->checksum_alg = binary_log::BINLOG_CHECKSUM_ALG_OFF;
  fdle.register_temp_buf(const_cast<char *>(fde.reader().buffer()), false);

  // unpack the event
  binary_log::transaction::compression::Iterable_buffer it(
      tple.get_payload(), tple.get_payload_size(), tple.get_uncompressed_size(),
      tple.get_compression_type());

  for (auto ptr : it) {
    Log_event *next = nullptr;
    size_t event_len = uint4korr(ptr + EVENT_LEN_OFFSET);
    if (binlog_event_deserialize(reinterpret_cast<const unsigned char *>(ptr),
                                 event_len, &fdle, true, &next)) {
      error = true; /* purecov: inspected */
      break;        /* purecov: inspected */
    } else {
      DBUG_ASSERT(next != nullptr);
      events.push_back(next);
    }
  }

  return error;
}

bool Mts_submode_database::set_multi_threaded_applier_context(
    const Relay_log_info &rli, Log_event &ev) {
  bool error = false;

  // if this is a transaction payload event, we need to set the proper
  // databases that its internal events update
  if (ev.get_type_code() == binary_log::TRANSACTION_PAYLOAD_EVENT) {
    Mts_db_names toset;
    bool max_mts_dbs_in_event = false;
    std::set<std::string> dbs;
    auto &tple = static_cast<Transaction_payload_log_event &>(ev);
    std::vector<Log_event *> events;
    unfold_transaction_payload_event(*rli.get_rli_description_event(), tple,
                                     events);

    for (auto inner : events) {
      Mts_db_names mts_dbs;

      // This transaction payload event is already marked to run in
      // isolation or the event being handled does not contain partition
      // information
      if (max_mts_dbs_in_event || !inner->contains_partition_info(true)) {
        delete inner;
        continue;
      }

      // The following queries should run in isolation, thence setting
      // OVER_MAX_DBS_IN_EVENT_MTS
      if ((inner->get_type_code() == binary_log::QUERY_EVENT)) {
        auto qev = static_cast<Query_log_event *>(inner);
        if (qev->is_query_prefix_match(STRING_WITH_LEN("XA COMMIT")) ||
            qev->is_query_prefix_match(STRING_WITH_LEN("XA ROLLBACK"))) {
          max_mts_dbs_in_event = true;
          delete inner;
          continue;
        }
      }

      // OK, now that we have ruled the exceptions, lets handle the databases
      // in the inner event.
      inner->get_mts_dbs(&mts_dbs, rli.rpl_filter);

      // inner event has mark to run in isolation
      if (mts_dbs.num == OVER_MAX_DBS_IN_EVENT_MTS) {
        max_mts_dbs_in_event = true;
        delete inner;
        continue;
      }

      // iterate over the databases and add them to the set
      for (int i = 0; i < mts_dbs.num; i++) {
        dbs.insert(mts_dbs.name[i]);
        if (dbs.size() == MAX_DBS_IN_EVENT_MTS) {
          max_mts_dbs_in_event = true;
          break;
        }
      }

      // inner event not needed anymore. Delete.
      delete inner;
    }

    // now set the database information in the event
    if (max_mts_dbs_in_event) {
      toset.name[0] = "\0";
      toset.num = OVER_MAX_DBS_IN_EVENT_MTS;
    } else {
      int i = 0;
      // set the databases
      for (auto &db : dbs) toset.name[i++] = db.c_str();

      // set the number of databases
      toset.num = dbs.size();
    }

    // save the mts_dbs to the payload event
    tple.set_mts_dbs(toset);
  }

  return error;
}

/**
 Logic to detach the temporary tables from the worker threads upon
 event execution.
 @param thd THD instance
 @param rli Relay_log_info pointer
 @param ev  Query_log_event that is being applied
*/
void Mts_submode_database::detach_temp_tables(THD *thd,
                                              const Relay_log_info *rli,
                                              Query_log_event *ev) {
  DBUG_TRACE;
  if (!is_mts_worker(thd)) return;
  int parts = ((ev->mts_accessed_dbs == OVER_MAX_DBS_IN_EVENT_MTS)
                   ? 1
                   : ev->mts_accessed_dbs);
  /*
    todo: optimize for a case of

    a. one db
       Only detaching temporary_tables from thd to entry would require
       instead of the double-loop below.

    b. unchanged thd->temporary_tables.
       In such case the involved entries would continue to hold the
       unmodified lists provided that the attach_ method does not
       destroy references to them.
  */
  for (int i = 0; i < parts; i++) {
    ev->mts_assigned_partitions[i]->temporary_tables = nullptr;
  }

  Rpl_filter *rpl_filter = rli->rpl_filter;
  for (TABLE *table = thd->temporary_tables; table;) {
    int i;
    const char *db_name = nullptr;

    // find which entry to go
    for (i = 0; i < parts; i++) {
      db_name = ev->mts_accessed_db_names[i];
      if (!strlen(db_name)) break;
      // Only default database is rewritten.
      if (!rpl_filter->is_rewrite_empty() && !strcmp(ev->get_db(), db_name)) {
        size_t dummy_len;
        const char *db_filtered =
            rpl_filter->get_rewrite_db(db_name, &dummy_len);
        // db_name != db_filtered means that db_name is rewritten.
        if (strcmp(db_name, db_filtered)) db_name = db_filtered;
      }
      if (strcmp(table->s->db.str, db_name) < 0)
        continue;
      else {
        // When rewrite db rules are used we can not rely on
        // mts_accessed_db_names elements order.
        if (!rpl_filter->is_rewrite_empty() &&
            strcmp(table->s->db.str, db_name))
          continue;
        else
          break;
      }
    }
    DBUG_ASSERT(db_name &&
                (!strcmp(table->s->db.str, db_name) || !strlen(db_name)));
    DBUG_ASSERT(i < ev->mts_accessed_dbs);
    // table pointer is shifted inside the function
    table = mts_move_temp_table_to_entry(table, thd,
                                         ev->mts_assigned_partitions[i]);
  }

  DBUG_ASSERT(!thd->temporary_tables);
#ifndef DBUG_OFF
  for (int i = 0; i < parts; i++) {
    DBUG_ASSERT(!ev->mts_assigned_partitions[i]->temporary_tables ||
                !ev->mts_assigned_partitions[i]->temporary_tables->prev);
  }
#endif
}

/**
  Logic to get least occupied worker when the sql mts_submode= database
  @param ws  array of worker threads
  @return slave worker thread
 */
Slave_worker *Mts_submode_database::get_least_occupied_worker(
    Relay_log_info *, Slave_worker_array *ws, Log_event *) {
  long usage = LONG_MAX;
  Slave_worker **ptr_current_worker = nullptr, *worker = nullptr;

  DBUG_TRACE;

#ifndef DBUG_OFF

  if (DBUG_EVALUATE_IF("mts_distribute_round_robin", 1, 0)) {
    worker = ws->at(w_rr % ws->size());
    LogErr(INFORMATION_LEVEL, ER_RPL_WORKER_ID_IS, worker->id,
           static_cast<ulong>(w_rr % ws->size()));
    DBUG_ASSERT(worker != nullptr);
    return worker;
  }
#endif

  for (Slave_worker **it = ws->begin(); it != ws->end(); ++it) {
    ptr_current_worker = it;
    if ((*ptr_current_worker)->usage_partition <= usage) {
      worker = *ptr_current_worker;
      usage = (*ptr_current_worker)->usage_partition;
    }
  }
  DBUG_ASSERT(worker != nullptr);
  return worker;
}

/* MTS submode master Default constructor */
Mts_submode_logical_clock::Mts_submode_logical_clock() {
  type = MTS_PARALLEL_TYPE_LOGICAL_CLOCK;
  first_event = true;
  force_new_group = false;
  is_new_group = true;
  delegated_jobs = 0;
  jobs_done = 0;
  last_lwm_timestamp = SEQ_UNINIT;
  last_lwm_index = INDEX_UNDEF;
  is_error = false;
  min_waited_timestamp = SEQ_UNINIT;
  last_committed = SEQ_UNINIT;
  sequence_number = SEQ_UNINIT;
}

/**
   The method finds the minimum logical timestamp (low-water-mark) of
   committed transactions.
   The successful search results in a pair of a logical timestamp value and a
   GAQ index that contains it. last_lwm_timestamp may still be raised though the
   search does not find any satisfying running index. Search is implemented as
   headway scanning of GAQ from a point of a previous search's stop position
   (last_lwm_index). Whether the cached (memorized) index value is considered to
   be stale when its timestamp gets less than the current "stable" LWM:

        last_lwm_timestamp <= GAQ.lwm.sequence_number           (*)

   Staleness is caused by GAQ garbage collection that increments the rhs of (*),
   see @c move_queue_head(). When that's diagnosed, the search in GAQ needs
   restarting from the queue tail.

   Formally, the undefined cached value of last_lwm_timestamp is also stale.

   @verbatim
              the last time index containg lwm
                  +------+
                  | LWM  |
                  |  |   |
                  V  V   V
   GAQ:   xoooooxxxxxXXXXX...X
                ^   ^
                |   | LWM+1
                |
                +- tne new current_lwm

         <---- logical (commit) time ----
   @endverbatim

   here `x' stands for committed, `X' for committed and discarded from
   the running range of the queue, `o' for not committed.

   @param  rli         Relay_log_info pointer
   @param  need_lock   Either the caller or the function must hold a mutex
                       to avoid race with concurrent GAQ update.

   @return possibly updated current_lwm
*/
longlong Mts_submode_logical_clock::get_lwm_timestamp(Relay_log_info *rli,
                                                      bool need_lock) {
  longlong lwm_estim;
  Slave_job_group *ptr_g = nullptr;
  bool is_stale = false;

  if (!need_lock) mysql_mutex_lock(&rli->mts_gaq_LOCK);

  /*
    Make the "stable" LWM-based estimate which will be compared
    against the cached "instant" value.
  */
  lwm_estim = rli->gaq->lwm.sequence_number;
  /*
    timestamp continuity invariant: if the queue has any item
    its timestamp is greater on one than the estimate.
  */
  DBUG_ASSERT(lwm_estim == SEQ_UNINIT || rli->gaq->empty() ||
              lwm_estim + 1 ==
                  rli->gaq->get_job_group(rli->gaq->entry)->sequence_number);

  last_lwm_index = rli->gaq->find_lwm(
      &ptr_g,
      /*
        The underfined "stable" forces the scan's restart
        as the stale value does.
      */
      lwm_estim == SEQ_UNINIT ||
              (is_stale = clock_leq(last_lwm_timestamp, lwm_estim))
          ? rli->gaq->entry
          : last_lwm_index);
  /*
    if the returned index is sane update the timestamp.
  */
  if (last_lwm_index != rli->gaq->size) {
    // non-decreasing lwm invariant
    DBUG_ASSERT(clock_leq(last_lwm_timestamp, ptr_g->sequence_number));

    last_lwm_timestamp = ptr_g->sequence_number;
  } else if (is_stale) {
    last_lwm_timestamp.store(lwm_estim);
  }

  if (!need_lock) mysql_mutex_unlock(&rli->mts_gaq_LOCK);

  return last_lwm_timestamp;
}

/**
   The method implements logical timestamp conflict detection
   and resolution through waiting by the calling thread.
   The conflict or waiting condition is like the following

           lwm < last_committed,

   where lwm is a minimum logical timestamp of committed transactions.
   Since the lwm's exact value is not always available its pessimistic
   estimate (an old version) is improved (get_lwm_timestamp()) as the
   first step before to the actual waiting commitment.

   Special cases include:

   When @c last_committed_arg is uninitialized the calling thread must
   proceed without waiting for anyone. Any possible dependency with unknown
   commit parent transaction shall be sorted out by the parent;

   When the gaq index is subsequent to the last lwm index
   there's no dependency of the current transaction with any regardless of
   lwm timestamp should it be SEQ_UNINIT.
   Consequently when GAQ consists of just one item there's none to wait.
   Such latter case is left to the caller to handle.

   @note The caller must make sure the current transaction won't be waiting
         for itself. That is the method should not be caller by a Worker
         whose group assignment is in the GAQ front item.

   @param rli relay log info of coordinator
   @param last_committed_arg  logical timestamp of a parent transaction
   @return false as success,
           true  when the error flag is raised or
                 the caller thread is found killed.
*/
bool Mts_submode_logical_clock::wait_for_last_committed_trx(
    Relay_log_info *rli, longlong last_committed_arg) {
  THD *thd = rli->info_thd;

  DBUG_TRACE;
  //last_committed_arg == 0，binlog文件中的第一个event，此时肯定不会与之前的group并行执行
  if (last_committed_arg == SEQ_UNINIT) return false;

  mysql_mutex_lock(&rli->mts_gaq_LOCK);

  DBUG_ASSERT(min_waited_timestamp == SEQ_UNINIT);

  min_waited_timestamp.store(last_committed_arg);
  /*
    This transaction is a candidate for insertion into the waiting list.
    That fact is descibed by incrementing waited_timestamp_cnt.
    When the candidate won't make it the counter is decremented at once
    while the mutex is hold.
  */
/*
get_lwm_timestamp（），获取当前gap中的lwr，等于当前gap中最新的连续committed 事务的sequence_number
              the last time index containg lwm
                  +------+
                  | LWM  |
                  |  |   |
                  V  V   V
   GAQ:   xoooooxxxxxXXXXX...X
                ^   ^
                |   | LWM+1
                |
                +- tne new current_lwm

         <---- logical (commit) time ----
*/
//等待，直到需要的last_committed_arg在gap中已被discard。
  if ((!rli->info_thd->killed && !is_error) &&
      !clock_leq(last_committed_arg, get_lwm_timestamp(rli, true))) {
    PSI_stage_info old_stage;
    struct timespec ts[2];
    set_timespec_nsec(&ts[0], 0);

    DBUG_ASSERT(rli->gaq->len >= 2);  // there's someone to wait

    thd->ENTER_COND(&rli->logical_clock_cond, &rli->mts_gaq_LOCK,
                    &stage_worker_waiting_for_commit_parent, &old_stage);
    do {
      mysql_cond_wait(&rli->logical_clock_cond, &rli->mts_gaq_LOCK);
    } while ((!rli->info_thd->killed && !is_error) &&
             !clock_leq(last_committed_arg, estimate_lwm_timestamp()));
    min_waited_timestamp.store(SEQ_UNINIT);  // reset waiting flag
    mysql_mutex_unlock(&rli->mts_gaq_LOCK);
    thd->EXIT_COND(&old_stage);
    set_timespec_nsec(&ts[1], 0);
    rli->mts_total_wait_overlap += diff_timespec(&ts[1], &ts[0]);
  } else {
    min_waited_timestamp.store(SEQ_UNINIT);
    mysql_mutex_unlock(&rli->mts_gaq_LOCK);
  }

  return rli->info_thd->killed || is_error;
}

/**
 Does necessary arrangement before scheduling next event.
 The method computes the meta-group status of the being scheduled
 transaction represented by the event argument. When the status
 is found OUT (of the current meta-group) as encoded as is_new_group == true
 the global Scheduler (Coordinator thread) requests full synchronization
 with all Workers.
 The current being assigned group descriptor gets associated with
 the group's logical timestamp aka sequence_number.

 @return ER_MTS_CANT_PARALLEL, ER_MTS_INCONSISTENT_DATA
          0 if no error or slave has been killed gracefully
 */
int Mts_submode_logical_clock::schedule_next_event(Relay_log_info *rli,
                                                   Log_event *ev) {
  //暂存上一次处理event的sequence_number
  longlong last_sequence_number = sequence_number;
  bool gap_successor = false;

  DBUG_TRACE;
  // We should check if the SQL thread was already killed before we schedule
  // the next transaction
  if (sql_slave_killed(rli->info_thd, rli)) return 0;

  Slave_job_group *ptr_group =
      rli->gaq->get_job_group(rli->gaq->assigned_group_index);
  /*
    A group id updater must satisfy the following:
    - A query log event ("BEGIN" ) or a GTID EVENT
    - A DDL or an implicit DML commit.
  */
  switch (ev->get_type_code()) {
    //当前处理event为GTID/匿名GTID event，也就是job group的第一个event，
    //将event的sequence_number和last_committed暂存起来，并赋值给job_group。
    case binary_log::GTID_LOG_EVENT:
    case binary_log::ANONYMOUS_GTID_LOG_EVENT:
      // TODO: control continuity
      ptr_group->sequence_number = sequence_number =
          static_cast<Gtid_log_event *>(ev)->sequence_number;
      ptr_group->last_committed = last_committed =
          static_cast<Gtid_log_event *>(ev)->last_committed;
      break;

    default:
      // binlog中，sequence_number、last_committed只出现在GTID/匿名GTID event中。
      // 只通过GTID/匿名GTID event的sequence_number、last_committed来决定
      // 此对应的job_group是否可与现有的map中的其他job_group并行执行
      // 进入到此方法的非GTID event对应的group，与之前的group串行执行，这可能是一些非常规event
      sequence_number = last_committed = SEQ_UNINIT;

      break;
  }

  DBUG_PRINT("info", ("sequence_number %lld, last_committed %lld",
                      sequence_number, last_committed));

  if (first_event) {
    // first_event在mts模式初始化时设置为true，
    // 这里表示mts模式切换后或者协调器初始化完成后的处理的第一个event
    first_event = false;
  } else {
    //这里的两个if的条件，正常情况都不会发生。除非发生上面default的情况时，sequence_number = last_committed = 0
    //否则sequence_number始终大于last_committed，并且sequence_number始终等于last_sequence_number+1
    if (unlikely(clock_leq(sequence_number, last_committed) &&
                 last_committed != SEQ_UNINIT)) {
      /* inconsistent (buggy) timestamps */
      LogErr(ERROR_LEVEL, ER_RPL_INCONSISTENT_TIMESTAMPS_IN_TRX,
             sequence_number, last_committed);
      return ER_MTS_CANT_PARALLEL;
    }
    if (unlikely(clock_leq(sequence_number, last_sequence_number) &&
                 sequence_number != SEQ_UNINIT)) {
      /* inconsistent (buggy) timestamps */
      LogErr(ERROR_LEVEL, ER_RPL_INCONSISTENT_SEQUENCE_NO_IN_TRX,
             sequence_number, last_sequence_number);
      return ER_MTS_CANT_PARALLEL;
    }
    /*
      Being scheduled transaction sequence may have gaps, even in
      relay log. In such case a transaction that succeeds a gap will
      wait for all ealier that were scheduled to finish. It's marked
      as gap successor now.
    */
    static_assert(SEQ_UNINIT == 0, "");
    //当sequence_number > last_sequence_number+1,表示event之间发生了产生了间隙，
    //什么情况下会发生？
    if (unlikely(sequence_number > last_sequence_number + 1)) {
      /*
        TODO: account autopositioning
        DBUG_ASSERT(rli->replicate_same_server_id);
      */
      DBUG_PRINT("info", ("sequence_number gap found, "
                          "last_sequence_number %lld, sequence_number %lld",
                          last_sequence_number, sequence_number));
      //标记sequence_number与last_sequence_number之间发生了间隙
      gap_successor = true;
    }
  }

  /*
    The new group flag is practically the same as the force flag
    when up to indicate syncronization with Workers.
  */
  is_new_group =
      (/* First event after a submode switch; */
       first_event ||
       /* Require a fresh group to be started; */
       // todo: turn `force_new_group' into sequence_number == SEQ_UNINIT
       // condition
       //force_new_group在int Rotate_log_event::do_update_pos(Relay_log_info *rli)方法中被设置为true
       force_new_group ||
       /* Rewritten event without commit point timestamp (todo: find use case)
        */
       // 正常记录在binlog中的event，sequence_number是从1开始的，这里指上面default的情况
       sequence_number == SEQ_UNINIT ||
       /*
         undefined parent (e.g the very first trans from the master),
         or old master.
       */
       //last_committed==0发生在binlog文件中的第一个event，或者上面default的情况
       last_committed == SEQ_UNINIT ||
       /*
         When gap successor depends on a gap before it the scheduler has
         to serialize this transaction execution with previously
         scheduled ones. Below for simplicity it's assumed that such
         gap-dependency is always the case.
       */
       //上面标记的sequence_number间隙
       gap_successor ||
       /*
         previous group did not have sequence number assigned.
         It's execution must be finished until the current group
         can be assigned.
         Dependency of the current group on the previous
         can't be tracked. So let's wait till the former is over.
       */
       //上一个处理的event的last_sequence_number==0，这个还不清楚是什么场景，但是无论如何，本次处理的job_group也不能并行处理了。
       last_sequence_number == SEQ_UNINIT);
  /*
    The coordinator waits till all transactions on which the current one
    depends on are applied.
  */
  //不是binlog的第一个event，且不是mts初始化后的第一个event， 并且是GTID event ，且sequence_number没有间隙。
  //总之肯定是个GTID event
  if (!is_new_group) {
    longlong lwm_estimate = estimate_lwm_timestamp();//已经计算好的lwm进行load()。内存栅栏。
    // 当前event的last_committed大于当前gap的check_point低水线并且当前gap要出队的不是当前的group
    // 正如下面注释说的，这是不可能的分支，gaq中怎么会同时包含不能并行执行的事务？
    if (!clock_leq(last_committed, lwm_estimate) &&
        rli->gaq->assigned_group_index != rli->gaq->entry) {
      /*
        "Unlikely" branch.

        The following block improves possibly stale lwm and when the
        waiting condition stays, recompute min_waited_timestamp and go
        waiting.
        At awakening set min_waited_timestamp to commit_parent in the
        subsequent GAQ index (could be NIL).
      */
      //如果当前event的last_committed（也即parent transaction）没有处于gap中的连续commited的lwm，
     //说明还有之前事务的sequence_number=当前事务的last_committed的事务尚未提交，父事务未提交，子事务不能与之并行执行。需要等待。
      if (wait_for_last_committed_trx(rli, last_committed)) {
        /*
          MTS was waiting for a dependent transaction to finish but either it
          has failed or the applier was requested to stop. In any case, this
          transaction wasn't started yet and should not warn about the
          coordinator stopping in a middle of a transaction to avoid polluting
          the server error log.
        */
        rli->reported_unsafe_warning = true;
        return -1;
      }
      /*
        Making the slave's max last committed (lwm) to satisfy this
        transaction's scheduling condition.
      */
      //这里有没看明白
      if (gap_successor) last_lwm_timestamp = sequence_number - 1;
      DBUG_ASSERT(!clock_leq(sequence_number, estimate_lwm_timestamp()));
    }
   //分配事务数++
    delegated_jobs++;

    DBUG_ASSERT(!force_new_group);
  } else {
    //is_new_group==true，当前的event代表的job_group不能与之前的job_group并行处理了，
    //需要等待，直到之前所有的job_groups处理完毕
    DBUG_ASSERT(delegated_jobs >= jobs_done);  //有未处理完的job_groups
    //待处理的job_groups加一个当前入队gap的group  == 完成的group+正在处理的group
    DBUG_ASSERT(is_error || (rli->gaq->len + jobs_done == 1 + delegated_jobs));
    //当前有未处理完毕的event
    DBUG_ASSERT(rli->mts_group_status == Relay_log_info::MTS_IN_GROUP);

    /*
      Under the new group fall the following use cases:
      - events from an OLD (sequence_number unaware) master;
      - malformed (missed BEGIN or GTID_NEXT) group incl. its
        particular form of CREATE..SELECT..from..@user_var (or rand- and
        int- var in place of @user- var).
        The malformed group is handled exceptionally each event is executed
        as a solitary group yet by the same (zero id) worker.
    */
    //通过delegated_jobs == jobs_done，要等待所有已分配worker的group执行完毕，注意，当前event对应的group还没有分配worker执行
    if (-1 == wait_for_workers_to_finish(rli)) return ER_MTS_INCONSISTENT_DATA;

    rli->mts_group_status = Relay_log_info::MTS_IN_GROUP;  // wait set it to NOT
    DBUG_ASSERT(min_waited_timestamp == SEQ_UNINIT);
    /*
      the instant last lwm timestamp must reset when force flag is up.
    */
    //当重新开始一个新的并行事务组，低水线job_group的sn重置为0
    rli->gaq->lwm.sequence_number = last_lwm_timestamp = SEQ_UNINIT;
    //当前gap中仅剩本次处理的group一个了
    delegated_jobs = 1;
    jobs_done = 0;
    //重置，方便下一次使用
    force_new_group = false;
    /*
      Not sequenced event can be followed with a logically relating
      e.g User var to be followed by CREATE table.
      It's supported to be executed in one-by-one fashion.
      Todo: remove with the event group parser worklog.
    */
    //上面default的情况。此时gap中只有它一个group了，这里给当前group分配一个slave_worker，都是空的，因此取第一个。
    if (sequence_number == SEQ_UNINIT && last_committed == SEQ_UNINIT)
    // 除此之外，还有另外一个地方对last_assigned_worker进行了分配
      rli->last_assigned_worker = *rli->workers.begin();
  }

#ifndef DBUG_OFF
  mysql_mutex_lock(&rli->mts_gaq_LOCK);
  DBUG_ASSERT(is_error || (rli->gaq->len + jobs_done == delegated_jobs));
  mysql_mutex_unlock(&rli->mts_gaq_LOCK);
#endif
  return 0;
}

/**
 Logic to attach the temporary tables from the worker threads upon
 event execution.
 @param thd THD instance
 @param rli Relay_log_info instance
 @param ev  Query_log_event that is being applied
*/
void Mts_submode_logical_clock::attach_temp_tables(THD *thd,
                                                   const Relay_log_info *rli,
                                                   Query_log_event *ev) {
  bool shifted = false;
  TABLE *table, *cur_table;
  DBUG_TRACE;
  if (!is_mts_worker(thd) || (ev->ends_group() || ev->starts_group())) return;
  /* fetch coordinator's rli */
  Relay_log_info *c_rli = static_cast<const Slave_worker *>(rli)->c_rli;
  DBUG_ASSERT(!thd->temporary_tables);
  mysql_mutex_lock(&c_rli->mts_temp_table_LOCK);
  if (!(table = c_rli->info_thd->temporary_tables)) {
    mysql_mutex_unlock(&c_rli->mts_temp_table_LOCK);
    return;
  }
  c_rli->info_thd->temporary_tables = nullptr;
  do {
    /* store the current table */
    cur_table = table;
    /* move the table pointer to next in list, so that we can isolate the
    current table */
    table = table->next;
    std::pair<uint, my_thread_id> st_id_pair =
        get_server_and_thread_id(cur_table);
    if (thd->server_id == st_id_pair.first &&
        thd->variables.pseudo_thread_id == st_id_pair.second) {
      /* short the list singling out the current table */
      if (cur_table->prev)  // not the first node
        cur_table->prev->next = cur_table->next;
      if (cur_table->next)  // not the last node
        cur_table->next->prev = cur_table->prev;
      /* isolate the table */
      cur_table->prev = nullptr;
      cur_table->next = nullptr;
      mts_move_temp_tables_to_thd(thd, cur_table);
    } else
        /* We must shift the C->temp_table pointer to the fist table unused in
           this iteration. If all the tables have ben used C->temp_tables will
           point to NULL */
        if (!shifted) {
      c_rli->info_thd->temporary_tables = cur_table;
      shifted = true;
    }
  } while (table);
  mysql_mutex_unlock(&c_rli->mts_temp_table_LOCK);
}

/**
 Logic to detach the temporary tables from the worker threads upon
 event execution.
 @param thd THD instance
 @param rli Relay_log_info instance
*/
void Mts_submode_logical_clock::detach_temp_tables(THD *thd,
                                                   const Relay_log_info *rli,
                                                   Query_log_event *) {
  DBUG_TRACE;
  if (!is_mts_worker(thd)) return;
  /*
    Here in detach section we will move the tables from the worker to the
    coordinaor thread. Since coordinator is shared we need to make sure that
    there are no race conditions which may lead to assert failures and
    non-deterministic results.
  */
  Relay_log_info *c_rli = static_cast<const Slave_worker *>(rli)->c_rli;
  mysql_mutex_lock(&c_rli->mts_temp_table_LOCK);
  mts_move_temp_tables_to_thd(c_rli->info_thd, thd->temporary_tables);
  mysql_mutex_unlock(&c_rli->mts_temp_table_LOCK);
  thd->temporary_tables = nullptr;
}

/**
  Logic to get least occupied worker when the sql mts_submode= master_parallel
  @param rli relay log info of coordinator
  @param ws  array of worker threads
  @param ev  event for which we are searching for a worker.
  @return slave worker thread or NULL when coordinator is killed by any worker.
 */

Slave_worker *Mts_submode_logical_clock::get_least_occupied_worker(
    Relay_log_info *rli, Slave_worker_array *ws MY_ATTRIBUTE((unused)),
    Log_event *ev) {
  Slave_worker *worker = nullptr;
  PSI_stage_info *old_stage = nullptr;
  THD *thd = rli->info_thd;
  DBUG_TRACE;
#ifndef DBUG_OFF

  if (DBUG_EVALUATE_IF("mts_distribute_round_robin", 1, 0)) {
    worker = ws->at(w_rr % ws->size());
    LogErr(INFORMATION_LEVEL, ER_RPL_WORKER_ID_IS, worker->id,
           static_cast<ulong>(w_rr % ws->size()));
    DBUG_ASSERT(worker != nullptr);
    return worker;
  }
  Slave_committed_queue *gaq = rli->gaq;
  Slave_job_group *ptr_group;
  ptr_group = gaq->get_job_group(rli->gaq->assigned_group_index);
#endif
  /*
    The scheduling works as follows, in this sequence
      -If this is an internal event of a transaction  use the last assigned
        worker
      -If the i-th transaction is being scheduled in this group where "i" <=
       number of available workers then schedule the events to the consecutive
       workers
      -If the i-th transaction is being scheduled in this group where "i" >
       number of available workers then schedule this to the forst worker that
       becomes free.
   */
  if (rli->last_assigned_worker) {
    worker = rli->last_assigned_worker;
    DBUG_ASSERT(ev->get_type_code() != binary_log::USER_VAR_EVENT ||
                worker->id == 0 || rli->curr_group_seen_begin ||
                rli->curr_group_seen_gtid);
  } else {
    worker = get_free_worker(rli);

    DBUG_ASSERT(ev->get_type_code() != binary_log::USER_VAR_EVENT ||
                rli->curr_group_seen_begin || rli->curr_group_seen_gtid);

    if (worker == nullptr) {
      struct timespec ts[2];

      set_timespec_nsec(&ts[0], 0);
      // Update thd info as waiting for workers to finish.
      thd->enter_stage(&stage_slave_waiting_for_workers_to_process_queue,
                       old_stage, __func__, __FILE__, __LINE__);
      while (!worker && !thd->killed) {
        /*
          Busy wait with yielding thread control before to next attempt
          to find a free worker. As of current, a worker
          can't have more than one assigned group of events in its
          queue.

          todo: replace this At-Most-One assignment policy with
                First Available Worker as
                this method clearly can't be considered as optimal.
        */
#if !defined(_WIN32)
        sched_yield();
#else
        my_sleep(rli->mts_coordinator_basic_nap);
#endif
        worker = get_free_worker(rli);
      }
      THD_STAGE_INFO(thd, *old_stage);
      set_timespec_nsec(&ts[1], 0);
      rli->mts_total_wait_worker_avail += diff_timespec(&ts[1], &ts[0]);
      rli->mts_wq_no_underrun_cnt++;
      /*
        Even OPTION_BEGIN is set, the 'BEGIN' event is not dispatched to
        any worker thread. So The flag is removed and Coordinator thread
        will not try to finish the group before abort.
      */
      if (worker == nullptr)
        rli->info_thd->variables.option_bits &= ~OPTION_BEGIN;
    }
    if (rli->get_commit_order_manager() != nullptr && worker != nullptr)
      rli->get_commit_order_manager()->register_trx(worker);
  }

  DBUG_ASSERT(ptr_group);
  // assert that we have a worker thread for this event or the slave has
  // stopped.
  DBUG_ASSERT(worker != nullptr || thd->killed);
  /* The master my have send  db partition info. make sure we never use them*/
  if (ev->get_type_code() == binary_log::QUERY_EVENT)
    static_cast<Query_log_event *>(ev)->mts_accessed_dbs = 0;

  return worker;
}

/**
  Protected method to fetch a worker having no events assigned.
  The method is supposed to be called by Coordinator, therefore
  comparison like w_i->jobs.len == 0 must (eventually) succeed.

  todo: consider to optimize scan that is getting more expensive with
  more # of Workers.

  @return  a pointer to Worker or NULL if none is free.
*/
Slave_worker *Mts_submode_logical_clock::get_free_worker(Relay_log_info *rli) {
  for (Slave_worker **it = rli->workers.begin(); it != rli->workers.end();
       ++it) {
    Slave_worker *w_i = *it;
    if (w_i->jobs.len == 0) return w_i;
  }
  return nullptr;
}

/**
  Waits for slave workers to finish off the pending tasks before returning.
  Used in this submode to make sure that all assigned jobs have been done.

  @param rli  coordinator rli.
  @param ignore worker to ignore.
  @return -1 for error.
           0 no error.
 */
int Mts_submode_logical_clock::wait_for_workers_to_finish(
    Relay_log_info *rli, MY_ATTRIBUTE((unused)) Slave_worker *ignore) {
  PSI_stage_info *old_stage = nullptr;
  THD *thd = rli->info_thd;
  DBUG_TRACE;
  DBUG_PRINT("info", ("delegated %d, jobs_done %d", delegated_jobs, jobs_done));
  // Update thd info as waiting for workers to finish.
  thd->enter_stage(&stage_slave_waiting_for_workers_to_process_queue, old_stage,
                   __func__, __FILE__, __LINE__);
  while (delegated_jobs > jobs_done && !thd->killed && !is_error) {
    // Todo: consider to replace with a. GAQ::get_lwm_timestamp() or
    // b. (better) pthread wait+signal similarly to DB type.
    if (mts_checkpoint_routine(rli, true)) return -1;
  }

  // Check if there is a failure on a not-ignored Worker
  for (Slave_worker **it = rli->workers.begin(); it != rli->workers.end();
       ++it) {
    Slave_worker *w_i = *it;
    if (w_i->running_status != Slave_worker::RUNNING) return -1;
  }

  DBUG_EXECUTE_IF("wait_for_workers_to_finish_after_wait", {
    const char act[] = "now WAIT_FOR coordinator_continue";
    DBUG_ASSERT(!debug_sync_set_action(rli->info_thd, STRING_WITH_LEN(act)));
  });

  // The current commit point sequence may end here (e.g Rotate to new log)
  rli->gaq->lwm.sequence_number = SEQ_UNINIT;
  // Restore previous info.
  THD_STAGE_INFO(thd, *old_stage);
  DBUG_PRINT("info", ("delegated %d, jobs_done %d, Workers have finished their"
                      " jobs",
                      delegated_jobs, jobs_done));
  rli->mts_group_status = Relay_log_info::MTS_NOT_IN_GROUP;
  return !thd->killed && !is_error ? 0 : -1;
}

/**
  Protected method to fetch the server_id and pseudo_thread_id from a
  temporary table
  @param  table instance pointer of TABLE structure.
  @return std:pair<uint, my_thread_id>
  @note   It is the caller's responsibility to make sure we call this
          function only for temp tables.
 */
std::pair<uint, my_thread_id>
Mts_submode_logical_clock::get_server_and_thread_id(TABLE *table) {
  DBUG_TRACE;
  const char *extra_string = table->s->table_cache_key.str;
  size_t extra_string_len = table->s->table_cache_key.length;
  // assert will fail when called with non temporary tables.
  DBUG_ASSERT(table->s->table_cache_key.length > 0);
  std::pair<uint, my_thread_id> ret_pair = std::make_pair(
      /* last 8  bytes contains the server_id + pseudo_thread_id */
      // fetch first 4 bytes to get the server id.
      uint4korr(extra_string + extra_string_len - 8),
      /* next  4 bytes contains the pseudo_thread_id */
      uint4korr(extra_string + extra_string_len - 4));
  return ret_pair;
}
