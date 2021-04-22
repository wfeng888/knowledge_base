plugin.cc

struct st_mysql_group_replication group_replication_descriptor = {
    MYSQL_GROUP_REPLICATION_INTERFACE_VERSION,
    plugin_group_replication_start,
    plugin_group_replication_stop,
    plugin_is_group_replication_running,
    plugin_is_group_replication_cloning,
    plugin_group_replication_set_retrieved_certification_info,
    plugin_get_connection_status,
    plugin_get_group_members,
    plugin_get_group_member_stats,
    plugin_get_group_members_number,
};


mysql_declare_plugin(group_replication_plugin){
    MYSQL_GROUP_REPLICATION_PLUGIN,
    &group_replication_descriptor,
    "group_replication",
    PLUGIN_AUTHOR_ORACLE,
    "Group Replication (1.1.0)", /* Plugin name with full version*/
    PLUGIN_LICENSE_GPL,
    plugin_group_replication_init,            /* Plugin Init */
    plugin_group_replication_check_uninstall, /* Plugin Check uninstall */
    plugin_group_replication_deinit,          /* Plugin Deinit */
    0x0101,                                   /* Plugin Version: major.minor */
    group_replication_status_vars,            /* status variables */
    group_replication_system_vars,            /* system variables */
    nullptr,                                  /* config options */
    0,                                        /* flags */
} mysql_declare_plugin_end;


int plugin_group_replication_init(MYSQL_PLUGIN plugin_info) {
	.........
transaction_consistency_manager = new Transaction_consistency_manager();
view_change_notifier = new Plugin_gcs_view_modification_notifier();
  gcs_module = new Gcs_operations();
  autorejoin_module = new Autorejoin_thread();
  ......
}


(server_state_delegate::Server_state_delegate)->observer_info_list{server_state_observer:Server_state_observer}
Server_state_observer server_state_observer = {
    sizeof(Server_state_observer),
    group_replication_before_handle_connection,  // before the client connects
                                                 // to the server
    group_replication_before_recovery,           // before recovery
    group_replication_after_engine_recovery,     // after engine recovery
    group_replication_after_recovery,            // after recovery
    group_replication_before_server_shutdown,    // before shutdown
    group_replication_after_server_shutdown,     // after shutdown
    group_replication_after_dd_upgrade,          // after DD upgrade from 5.7
};


(transaction_delegate::Trans_delegate)->observer_info_list{trans_observer::Trans_observer}
Trans_observer trans_observer = {
    sizeof(Trans_observer),

    group_replication_trans_before_dml,
    group_replication_trans_before_commit,
    group_replication_trans_before_rollback,
    group_replication_trans_after_commit,
    group_replication_trans_after_rollback,
    group_replication_trans_begin,
}



(binlog_transmit_delegate::Binlog_transmit_delegate)->observer_info_list{binlog_transmit_observer::Binlog_transmit_observer}
Binlog_transmit_observer binlog_transmit_observer = {
    sizeof(Binlog_transmit_observer),
    group_replication_transmit_start,     // transmit_start,
    group_replication_transmit_stop,      // transmit_stop,
    group_replication_reserve_header,     // reserve_header,
    group_replication_before_send_event,  // before_send_event,
    group_replication_after_send_event,   // after_send_event,
    group_replication_reset_master_logs   // reset_master
}



(group_events_observation_manager::Group_events_observation_manager)->group_events_observers{group_action_coordinator::Group_action_coordinator,}
Group_event_observer
	Group_action_coordinator
	Multi_primary_migration_action
	Primary_election_action
	Primary_election_primary_process
	Primary_election_secondary_process
	Primary_election_validation_handler
	Remote_clone_handler




(channel_observation_manager_list::Channel_observation_manager_list)->channel_observation_manager_list{(channel_manager::Channel_observation_manager)->channel_observers{observer::Channel_state_observer,},}
Channel_state_observer
	Applier_channel_state_observer
	Asynchronous_channels_state_observer
	Recovery_channel_state_observer





(group_transaction_observation_manager::Group_transaction_observation_manager)->group_transaction_listeners{
    (transaction_consistency_manager::Transaction_consistency_manager::Group_transaction_listener)
    ,(::Server_ongoing_transactions_handler::Group_transaction_listener)}
class Group_transaction_listener {
 public:
  /** Enum for transaction origins */
  enum enum_transaction_origin {
    GROUP_APPLIER_TRANSACTION = 0,   // Group applier transaction
    GROUP_RECOVERY_TRANSACTION = 1,  // Distributed recovery transaction
    GROUP_LOCAL_TRANSACTION = 2      // Local transaction
  };
  virtual int before_transaction_begin(my_thread_id thread_id,
                                       ulong gr_consistency_level,
                                       ulong hold_timeout,
                                       enum_rpl_channel_type channel_type) = 0;
  virtual int before_commit(my_thread_id thread_id,
                            enum_transaction_origin origin) = 0;
  virtual int before_rollback(my_thread_id thread_id,
                              enum_transaction_origin origin) = 0;
  virtual int after_commit(my_thread_id thread_id, rpl_sidno sidno,
                           rpl_gno gno) = 0;
  virtual int after_rollback(my_thread_id thread_id) = 0;
}