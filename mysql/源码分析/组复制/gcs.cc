Plugin_gcs_events_handler : public Gcs_communication_event_listener,
                                  public Gcs_control_event_listener



class Gcs_communication_event_listener {
 public:
  /**
    This method is called whenever a message is to be delivered.

    @param[in] message The received message. This is currently a
                       reference to avoid the existence of dangling
                       pointers.
  */

  virtual void on_message_received(const Gcs_message &message) const = 0;
};


class Gcs_control_event_listener {
 public:
  /**
    This method is called when the view is ready to be installed.

    The contents of Exchanged_data will be released by MySQL GCS
    after this handler finishes. Therefore the application MUST
    copy the contents of exchanged_data if it needs it at a later
    stage.

    @param[in] new_view       a reference to the new view.
    @param[in] exchanged_data the data handed out by other members.
  */

  virtual void on_view_changed(const Gcs_view &new_view,
                               const Exchanged_data &exchanged_data) const = 0;

  /**
    This method is called when the Data Exchange is about to happen in order
    for the client to provide which data it wants to exchange with the group.

    @return a reference to the exchangeable data. This is a pointer that must
            be deallocated by the caller, so please provide always a copy of
            the data to exchange.
  */

  virtual Gcs_message_data *get_exchangeable_data() const = 0;

  /**
    This member function is called when the set of suspicions
    has changed in the underlaying communication infrastructure.

    @param[in] members Contains the list of all members that are in the
                       current view.
    @param[in] unreachable Contains the list of members that are
                           unreachable in the current view, i.e., a subset
                           of @c members.
   */
  virtual void on_suspicions(
      const std::vector<Gcs_member_identifier> &members,
      const std::vector<Gcs_member_identifier> &unreachable) const = 0;
}











gcs_xcom_group_interfaces *Gcs_xcom_interface::get_group_interfaces(
    const Gcs_group_identifier &group_identifier) {
  if (!is_initialized()) return nullptr;

  // Try and retrieve already instantiated group interfaces for a certain group
  map<std::string, gcs_xcom_group_interfaces *>::const_iterator
      registered_group;
  registered_group = m_group_interfaces.find(group_identifier.get_group_id());

  gcs_xcom_group_interfaces *group_interface = nullptr;
  if (registered_group == m_group_interfaces.end()) {
    /*
      Retrieve some initialization parameters.
    */
    const std::string *join_attempts_str =
        m_initialization_parameters.get_parameter("join_attempts");
    const std::string *join_sleep_time_str =
        m_initialization_parameters.get_parameter("join_sleep_time");

    /*
      If the group interfaces do not exist, create and add them to
      the dictionary.
    */
    group_interface = new gcs_xcom_group_interfaces();
    m_group_interfaces[group_identifier.get_group_id()] = group_interface;

    Gcs_xcom_statistics *stats = new Gcs_xcom_statistics();

    group_interface->statistics_interface = stats;

    Gcs_xcom_view_change_control_interface *vce =
        new Gcs_xcom_view_change_control();

    auto *xcom_communication = new Gcs_xcom_communication(
        stats, s_xcom_proxy, vce, gcs_engine, group_identifier);
    group_interface->communication_interface = xcom_communication;

    Gcs_xcom_state_exchange_interface *se =
        new Gcs_xcom_state_exchange(group_interface->communication_interface);

    Gcs_xcom_group_management *xcom_group_management =
        new Gcs_xcom_group_management(s_xcom_proxy, group_identifier);
    group_interface->management_interface = xcom_group_management;

    Gcs_xcom_control *xcom_control = new Gcs_xcom_control(
        m_node_address, m_xcom_peers, group_identifier, s_xcom_proxy,
        xcom_group_management, gcs_engine, se, vce, m_boot, m_socket_util);
    group_interface->control_interface = xcom_control;

    xcom_control->set_join_behavior(
        static_cast<unsigned int>(atoi(join_attempts_str->c_str())),
        static_cast<unsigned int>(atoi(join_sleep_time_str->c_str())));

    // Set suspicion configuration parameters
    configure_suspicions_mgr(
        m_initialization_parameters,
        static_cast<Gcs_xcom_control *>(group_interface->control_interface)
            ->get_suspicions_manager());

    // Store the created objects for later deletion
    group_interface->vce = vce;
    group_interface->se = se;

    configure_message_stages(group_identifier);
  } else {
    group_interface = registered_group->second;
  }

  return group_interface;
}





typedef struct xcom_group_interfaces {
  Gcs_control_interface *control_interface;
  Gcs_communication_interface *communication_interface;
  Gcs_statistics_interface *statistics_interface;
  Gcs_group_management_interface *management_interface;

  /*
    Additional storage of group interface auxiliary structures for later
    deletion.
  */
  Gcs_xcom_view_change_control_interface *vce;
  Gcs_xcom_state_exchange_interface *se;

} gcs_xcom_group_interfaces;