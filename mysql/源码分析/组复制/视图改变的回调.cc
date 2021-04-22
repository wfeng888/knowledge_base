
#gcs_event_handler.cc
void Plugin_gcs_events_handler::on_view_changed(
    const Gcs_view &new_view, const Exchanged_data &exchanged_data) const {
  bool is_leaving = is_member_on_vector(new_view.get_leaving_members(),
                                        local_member_info->get_gcs_member_id());

  bool is_primary =
      (local_member_info->in_primary_mode() &&
       local_member_info->get_role() == Group_member_info::MEMBER_ROLE_PRIMARY);

  bool is_joining = is_member_on_vector(new_view.get_joined_members(),
                                        local_member_info->get_gcs_member_id());

  bool skip_election = false;
  enum_primary_election_mode election_mode = DEAD_OLD_PRIMARY;
  std::string suggested_primary("");
  // Was member expelled from the group due to network failures?
  if (this->was_member_expelled_from_group(new_view)) {
    DBUG_ASSERT(is_leaving);
    group_events_observation_manager->after_view_change(
        new_view.get_joined_members(), new_view.get_leaving_members(),
        new_view.get_members(), is_leaving, &skip_election, &election_mode,
        suggested_primary);
    goto end;
  }

  // An early error on the applier can render the join invalid
  if (is_joining &&
      local_member_info->get_recovery_status() ==
          Group_member_info::MEMBER_ERROR &&
      !autorejoin_module->is_autorejoin_ongoing()) {
    LogPluginErr(ERROR_LEVEL, ER_GRP_RPL_MEMBER_EXIT_PLUGIN_ERROR);
    gcs_module->notify_of_view_change_cancellation(
        GROUP_REPLICATION_CONFIGURATION_ERROR);
  } else {
    /*
      This code is present on on_view_changed and on_suspicions as no assumption
      can be made about the order in which these methods are invoked.
    */
    if (!is_leaving && group_partition_handler->is_member_on_partition()) {
      if (group_partition_handler->abort_partition_handler_if_running()) {
        LogPluginErr(WARNING_LEVEL, ER_GRP_RPL_CHANGE_GRP_MEM_NOT_PROCESSED);
        goto end;
      } else {
        /* If it was not running or we canceled it in time */
        LogPluginErr(WARNING_LEVEL, ER_GRP_RPL_MEMBER_CONTACT_RESTORED);
      }
    }

    /*
      Maybe on_suspicions we already executed the above block but it was too
      late. No point in repeating the message, but we need to break the view
      install.
    */
    if (!is_leaving &&
        group_partition_handler->is_partition_handling_terminated())
      goto end;

    if (!is_leaving && new_view.get_leaving_members().size() > 0)
      log_members_leaving_message(new_view);

    // update the Group Manager with all the received states
    if (update_group_info_manager(new_view, exchanged_data, is_joining,
                                  is_leaving) &&
        is_joining) {
      gcs_module->notify_of_view_change_cancellation();
      return;
    }

    if (!is_joining && new_view.get_joined_members().size() > 0)
      log_members_joining_message(new_view);

    // enable conflict detection if someone on group have it enabled
    if (local_member_info->in_primary_mode() &&
        group_member_mgr->is_conflict_detection_enabled()) {
      Certifier_interface *certifier =
          this->applier_module->get_certification_handler()->get_certifier();
      certifier->enable_conflict_detection();
    }

    // Inform any interested handler that the view changed
    View_change_pipeline_action *vc_action =
        new View_change_pipeline_action(is_leaving);

    applier_module->handle_pipeline_action(vc_action);
    delete vc_action;

    // Update any recovery running process and handle state changes
    this->handle_leaving_members(new_view, is_joining, is_leaving);

    // Handle joining members
    this->handle_joining_members(new_view, is_joining, is_leaving);

    if (is_leaving) gcs_module->leave_coordination_member_left();

    // Signal that the injected view was delivered
    if (gcs_module->is_injected_view_modification())
      gcs_module->notify_of_view_change_end();

    group_events_observation_manager->after_view_change(
        new_view.get_joined_members(), new_view.get_leaving_members(),
        new_view.get_members(), is_leaving, &skip_election, &election_mode,
        suggested_primary);

    // Handle leader election if needed
    if (!skip_election && !is_leaving) {
      this->handle_leader_election_if_needed(election_mode, suggested_primary);
    }
  }

  if (!is_leaving) {
    std::string view_id_representation = "";
    Gcs_view *view = gcs_module->get_current_view();
    if (view != nullptr) {
      view_id_representation = view->get_view_id().get_representation();
      delete view;
    }
    disable_read_mode_for_compatible_members();
    LogPluginErr(
        SYSTEM_LEVEL, ER_GRP_RPL_MEMBER_CHANGE,
        group_member_mgr->get_string_current_view_active_hosts().c_str(),
        view_id_representation.c_str());
  } else {
    LogPluginErr(SYSTEM_LEVEL, ER_GRP_RPL_MEMBER_LEFT_GRP);
  }

end:
  /* if I am the primary and I am leaving, notify about role change */
  if (is_leaving && is_primary) {
    group_member_mgr->update_member_role(
        local_member_info->get_uuid(), Group_member_info::MEMBER_ROLE_SECONDARY,
        m_notification_ctx);
  }

  /* flag view change */
  m_notification_ctx.set_view_changed();
  if (is_leaving)
    /*
      The leave view is an optimistic and local view.
      Therefore its ID is not meaningful, since it is not
      a global one.
     */
    m_notification_ctx.set_view_id("");
  else
    m_notification_ctx.set_view_id(new_view.get_view_id().get_representation());

  /* trigger notification */
  notify_and_reset_ctx(m_notification_ctx);
}