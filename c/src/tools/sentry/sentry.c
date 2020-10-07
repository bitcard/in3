#include "sentry.h"
static uint8_t SENTRY_INIT = 0;

void init_sentry_once(sentry_conf_t* conf) {
  if (!SENTRY_INIT) {
    sentry_options_t* options = sentry_options_new();
    in3_log_info("sentry-init\n");
    sentry_options_set_environment(options, "Debug");
    sentry_options_set_release(options, IN3_VERSION);
    sentry_options_set_database_path(options, conf->db);
    sentry_options_set_debug(options, conf->debug);
    sentry_options_set_dsn(options, conf->dsn);
    sentry_init(options);
    SENTRY_INIT = 1;
  }
}

static in3_ret_t handle_sentry(void* cptr, in3_plugin_act_t action, void* arg) {
  sentry_conf_t* conf = cptr;
  switch (action) {
    case PLGN_ACT_LOG_ERROR: {
      init_sentry_once(conf);
      error_log_ctx_t* t     = arg;
      sentry_value_t object = sentry_value_new_object();
      sentry_value_set_by_key(object, "request_ctx", sentry_value_new_string(t->ctx_req));
      sentry_value_t   event = sentry_value_new_message_event(
          SENTRY_LEVEL_ERROR, IN3_VERSION, t->msg);
      sentry_event_value_add_stacktrace(event, NULL, 64);
      sentry_capture_event(event);
      in3_log_info("sentry-event-sent\n");
      return IN3_OK;
    }

    case PLGN_ACT_TERM: {
      in3_log_info("sentry-shutdown\n");
      sentry_shutdown();
      _free(conf);
      return IN3_EIGNORE;
    }

    default:
      return IN3_ENOTSUP;
  }

  return IN3_EIGNORE;
}

in3_ret_t in3_register_sentry(in3_t* c) {
  sentry_conf_t* sc = _calloc(1, sizeof(sentry_conf_t));
  sc->dsn           = getenv("DSN_SENTRY");
  sc->db            = ".sentry-native";
  sc->debug         = 0;
  sc->stack         = 20;
  return plugin_register(c, PLGN_ACT_LOG_ERROR | PLGN_ACT_TERM, handle_sentry, sc, false);
}