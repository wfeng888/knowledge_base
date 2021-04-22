Gcs_xcom_engine::push	11	gcs_xcom_notification.cc (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\plugin\group_replication\libmysqlgcs\src\bindings\xcom)	247
	cb_xcom_expel	13	gcs_xcom_interface.cc (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\plugin\group_replication\libmysqlgcs\src\bindings\xcom)	1628
	cb_xcom_receive_data	24	gcs_xcom_interface.cc (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\plugin\group_replication\libmysqlgcs\src\bindings\xcom)	1241
	cb_xcom_receive_global_view	24	gcs_xcom_interface.cc (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\plugin\group_replication\libmysqlgcs\src\bindings\xcom)	1398
	cb_xcom_receive_local_view	22	gcs_xcom_interface.cc (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\plugin\group_replication\libmysqlgcs\src\bindings\xcom)	1548
	Gcs_xcom_communication_protocol_changer::rollback_increment_nr_packets_in_transit	25	gcs_xcom_communication_protocol_changer.cc (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\plugin\group_replication\libmysqlgcs\src\bindings\xcom)	217
	Gcs_xcom_control::join	29	gcs_xcom_control_interface.cc (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\plugin\group_replication\libmysqlgcs\src\bindings\xcom)	268
	Gcs_xcom_control::leave	23	gcs_xcom_control_interface.cc (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\plugin\group_replication\libmysqlgcs\src\bindings\xcom)	631
	Gcs_xcom_engine::finalize	13	gcs_xcom_notification.cc (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\plugin\group_replication\libmysqlgcs\src\bindings\xcom)	192
	Gcs_xcom_engine::push		gcs_xcom_notification.h (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\plugin\group_replication\libmysqlgcs\src\bindings\xcom)	153
	Gcs_xcom_interface::initialize_xcom	163	gcs_xcom_interface.cc (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\plugin\group_replication\libmysqlgcs\src\bindings\xcom)	978
	gcs_xcom_notification_unittest::TEST_F	10	gcs_xcom_notification-t.cc (D:\wangfeng\Virtual_Machines\share\mysql-boost-8.0.23\mysql-8.0.23\unittest\gunit\libmysqlgcs\xcom)	58






#gcx_xcom_notification.cc
void Gcs_xcom_engine::process() {
  Gcs_xcom_notification *notification = nullptr;
  bool stop = false;

  while (!stop) {
    m_wait_for_notification_mutex.lock();
    while (m_notification_queue.empty()) {
      m_wait_for_notification_cond.wait(
          m_wait_for_notification_mutex.get_native_mutex());
    }
    notification = m_notification_queue.front();
    m_notification_queue.pop();
    m_wait_for_notification_mutex.unlock();

    MYSQL_GCS_LOG_TRACE("Started executing during regular phase: %p",
                        notification)
    stop = (*notification)();
    MYSQL_GCS_LOG_TRACE("Finish executing during regular phase: %p",
                        notification)
    delete notification;
  }
}
