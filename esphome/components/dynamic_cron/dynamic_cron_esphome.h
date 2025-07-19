//
// TODO: The next_expiry value showing in esphome, after an OTA update, is incorrect,
//       until the crontab string is changed, or the device is rebooted.
//       Note that the optional display of the multiple next_expiry values is NOT incorrect
//       at any point during this issue.
//       It is not known if the actual next-start-time is incorrect, or if this is a display issue.
//
// NOTE: ESPHome is now using esp-idf v5, which uses 64-bit long-long fot time_t.
//       This is different from esp-idf v4, which used 32-bit long.

#pragma once

//#include "Arduino.h"
#include <iostream>
#include <string>
#include <ctime>

#include "esphome/core/component.h"
#include "esphome/core/application.h"
#include "esphome/core/time.h"
#include "esphome/core/preferences.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/text/text.h"

#include "preference_wrapper.h"
#include "dynamic_cron.h"


namespace esphome {
namespace dynamic_cron {

// Forward Declarations:
//
// To push the sub-component building entirely into c++, we would need to separate
// the code into .h and .cpp files. Otherwise we get bad-use-of-incomplete-class
// errors at compile time. Currently not an issue, since we build subcomponents
// from the py code, which is the right way to do it in esphome, I think.
//
class BypassSwitch;
class RememberNextSwitch;
class CronNextSensor;
class CrontabText;

inline const size_t                    CRONTAB_MAX_LEN = 128;

// Static variable to track if the version has been logged at boot.
static bool                            version_logged = false;



class Schedule : public Component, public ScheduleCore {

protected:
  // If true, clears prefs at first boot after flash.
  bool                                 clear_prefs;
  
  // Preference set timestamp
  std::time_t                          initial_stamp;
  
  // Preference objects
  MyPreference<std::time_t>            initial_stamp_pref;
  MyPreference<bool>                   bypass_pref;
  MyPreference<bool>                   remember_next_pref;
  MyPreference<std::time_t>            next_expiry_pref;
  StringPreference<CRONTAB_MAX_LEN>    crontab_pref;

  bool                                 setup_complete;

public:
  
  // These hold pointers to the subcomponents.
  BypassSwitch                         *bypass_switch{nullptr};
  RememberNextSwitch                   *remember_next_switch{nullptr};
  CronNextSensor                       *next_expiry_sensor{nullptr};
  // TODO: Change CrontabText and crontab_text to CrontabTextField, crontab_text_field
  // Don't forget to update __init__.py
  CrontabText                          *crontab_text{nullptr};
  
  // Macro for defining setters for the above entity pointer variables.
  #define DEFINE_SETTER(MemberType, MemberName) \
    void set_##MemberName(MemberType *value) { this->MemberName = value; }
    
  DEFINE_SETTER(BypassSwitch, bypass_switch)
  DEFINE_SETTER(RememberNextSwitch, remember_next_switch)
  DEFINE_SETTER(CronNextSensor, next_expiry_sensor)
  DEFINE_SETTER(CrontabText, crontab_text)
  
  bool                                 last_bypass_state;
  bool                                 last_remember_next_state;
  std::string                          last_next_expiry_state;
  std::string                          last_crontab_text_state;
  
  // Custom constructor method to create Schedule object.
  // NOTE: The function-pointer argument must have NO captures, if it's receiving a lambda.
  // Otherwise, the lambda won't be converted to a simple function/pointer.
  // So, if you pass in a lambda, the [] must be empty.
  //
  Schedule( // schedule-name, schedule-id, target-action-lambda-or-function-pointer
    std::string _name,
    std::string _id,
    bool(*_target_action_fptr)()
  ) :
    ScheduleCore(_name, _id, _target_action_fptr),
    clear_prefs(false),
    last_bypass_state(false),
    last_remember_next_state(false),
    last_next_expiry_state(""),
    last_crontab_text_state("")
  {
    LOGI("Constructing cron schedule '%s' %s", _name.c_str(), _id.c_str());
  } // constructor.


  // Esphome Component overrides
  void setup() override {
    if (setup_complete || !timeIsValid())
        return;

    if (!version_logged) {
      printVersion();
      version_logged = true;
    }

    LOGD("Setup beginning for '%s' %s", schedule_name.c_str(), schedule_id.c_str());
    
    // Init prefs timestamp
    initial_stamp_pref.init(schedule_id);
    initial_stamp = initial_stamp_pref.load_with_default(TIMESTAMP);
    if (initial_stamp != TIMESTAMP &&
        (
          clear_prefs ||
          TIMESTAMP == 0 ||
          difftime(TIMESTAMP, initial_stamp) < 0
        )
    ) {
      initial_stamp = TIMESTAMP;
      initial_stamp_pref.save(initial_stamp);
    }
    
    // Init Bypass prefs
    bypass_pref.init(schedule_id + "_bypass_" + std::to_string(initial_stamp));
    bypass = bypass_pref.load_with_default(bypass_default);
    LOGI("Loaded bypass: %d", bypass);
    
    // Init RememberNext prefs
    remember_next_pref.init(schedule_id + "_remember_" + std::to_string(initial_stamp));
    remember_next = remember_next_pref.load_with_default(remember_next_default);
    LOGI("Loaded remember_next: %d", remember_next);
    
    // Init CronNext prefs
    next_expiry_pref.init(schedule_id + "_next_expiry_" + std::to_string(initial_stamp));
      if (remember_next && !bypass) {
        next_expiry = next_expiry_pref.load_with_default(0);
      } else {
        next_expiry_pref.save(0);
        next_expiry = 0;
      }
    LOGI("Loaded next_expiry: %lld", (long long)next_expiry);
    
    // Init Crontab prefs
    crontab_pref.init(schedule_id + "_crontab_" + std::to_string(initial_stamp));
    crontab = crontab_pref.load_with_default(crontab_default);
    LOGI("Loaded crontab: %s", crontab.c_str());

    
    if (timeIsValid()) {
      
      if (!timeIsValid(next_expiry)) {
        setNextExpiry();
      }
      
      // Push values to entity.
      updateEntityData(bypass_switch, last_bypass_state, getBypass());
      updateEntityData(remember_next_switch, last_remember_next_state, getRememberNext());
      updateEntityData(next_expiry_sensor, last_next_expiry_state, cronNextString("---"));
      updateEntityData(crontab_text, last_crontab_text_state, getCrontab());
      
      setup_complete = true;
      LOGD("[setup()] Setup complete for '%s' %s", schedule_name.c_str(), schedule_id.c_str());
    }
  } // setup()
  
  
  void loop() override {
    if (!timeIsValid())
        return;

    if (!setup_complete)
        setup();

    if (next_expiry == 0)
        return;

    std::time_t now = std::time(NULL);

    //LOGV("Looping: %li", now);

    if (std::difftime(now, next_expiry) >= 0)
      cronAction();

    LOGV("bypass_switch last: %d, crnt: %d", last_bypass_state, getBypass());
    updateEntityData(bypass_switch, last_bypass_state, getBypass());
    LOGV("remember_next_switch last: %d, crnt: %d", last_remember_next_state, getRememberNext());
    updateEntityData(remember_next_switch, last_remember_next_state, getRememberNext());
    LOGV("next_expiry_sensor last: %s, crnt: %s", last_next_expiry_state.c_str(), cronNextString("---").c_str());
    updateEntityData(next_expiry_sensor, last_next_expiry_state, cronNextString("---"));
    LOGV("crontab_text last: %s, crnt: %s", last_crontab_text_state.c_str(), getCrontab().c_str());
    updateEntityData(crontab_text, last_crontab_text_state, getCrontab());
  }


  void dump_config() override {
    // This method will trigger once for each schedule loaded by esphome,
    // but it does not trigger when running the tests.
    //
    //ESP_LOGCONFIG(LOGTAG, "Dynamic Cron Schedule");
    //LOGD("Dynamic Cron Schedule ");
  }


  void setClearPrefs(bool val) {
    clear_prefs = val;
  }
  
  
  // Overides base setters to include preference storage.
  
  bool setBypass(bool val) override {
    // Note that sched-core setBypass() always calls setNextExpiry().
    bool rslt = ScheduleCore::setBypass(val);
    updateEntityData(bypass_switch, last_bypass_state, rslt);
    bypass_pref.save(rslt);
    return rslt;
  }
  
  bool setRememberNext(bool val) override {
    bool rslt = ScheduleCore::setRememberNext(val);
    remember_next_pref.save(rslt);
    // If remember_next is toggled, we always want to write something to next_expiry_pref,
    // unless bypass is true (if bypass is true, next_expiry and next_expiry_pref should always be 0).
    updateEntityData(remember_next_switch, last_remember_next_state, rslt);
    if (!bypass) {
      if (remember_next == false) {
        next_expiry_pref.save(0);
      }
      else {
        next_expiry_pref.save(next_expiry);
      }
    }
    return rslt;
  }
  
  void setNextExpiry() override {
    ScheduleCore::setNextExpiry();
    updateEntityData(next_expiry_sensor, last_next_expiry_state, cronNextString("---"));
    // If next_expiry is changed, we only save it to prefs if remember next, or if it's 0.
    if (next_expiry == 0 || remember_next)
        next_expiry_pref.save(next_expiry);
  }
  
  std::string setCrontab(std::string str) override {
    // Note that sched-core setCrontab() always calls setNextExpiry().
    std::string rslt = ScheduleCore::setCrontab(str);
    updateEntityData(crontab_text, last_crontab_text_state,rslt);
    crontab_pref.save(rslt);
    return rslt;
  }
  
  // TODO: Handle 'setNextExpiry(std::time_t input)' signature, when we start using it.
  
  
protected:
  
  // Wrapper around ScheduleCore::timeIsValid()
  // Adds validating time against ESPTime
  bool timeIsValid(std::time_t now = std::time(NULL)) {
    if (!ScheduleCore::timeIsValid(now))
        return false;
    ESPTime esp_time = ESPTime::from_epoch_local(now);
    return esp_time.is_valid();
  }
  
  // Method template to update each entity only when data has changed.
  // Call this in the Schedule loop.
  //
  template<typename EntityT, typename StateT>
  void updateEntityData(EntityT *entity, StateT &last_state, const StateT &new_state) {
    if (!entity) return;  // guard against null
    //LOGD("updateEntityData(%s)", entity->get_object_id().c_str());
    if (new_state != last_state) {
      LOGD("updateEntityData(%s) publishing...", entity->get_object_id().c_str());
      entity->publish_state(new_state);
      last_state = new_state;
    }
  }

}; // Schedule class



// ESPHOME ENTITY SUB-COMPONENTS
//
// TODO: Refactor default icon settings – move them to .py file.
//       See here for refactoring default entity icons:
//       https://github.com/esphome/esphome/blob/dev/esphome/const.py

class BypassSwitch : public switch_::Switch, public Component, public LoggerLocal<BypassSwitch> {
public:
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            
  Schedule *schedule{nullptr};
  bool last_state{false};
  
  void setup() override {
    //set_disabled_by_default(false);
    //set_icon("mdi:timer-off-outline");
    // set_restore_mode(switch_::SWITCH_RESTORE_DISABLED);
    LOGV("setup(): %s", get_object_id().c_str());
  }
  
  void set_schedule(Schedule *_schedule) {
    schedule = _schedule;
  }
  
  void write_state(bool _state) {
    LOGD("BypassSwitch::write_state(): %d", _state);
    schedule->setBypass(_state);
  }
  
}; // BypassSwitch class


class RememberNextSwitch : public switch_::Switch, public Component, public LoggerLocal<RememberNextSwitch> {
public:
  
  Schedule *schedule{nullptr};
  bool last_state{false};

  void setup() override {
    //set_disabled_by_default(false);
    //set_icon("mdi:memory");
    //set_restore_mode(switch_::SWITCH_RESTORE_DISABLED);
    LOGV("setup(): %s", get_object_id().c_str());
  }

  void set_schedule(Schedule *_schedule) {
    schedule = _schedule;
  }
  
  void write_state(bool _state) {
    LOGD("RememberNextSwitch::write_state(): %d", _state);
    schedule->setRememberNext(_state);
  }
  
}; // RememberNextSwitch class


class CronNextSensor : public text_sensor::TextSensor, public Component, public LoggerLocal<CronNextSensor> {
public:
  
  Schedule *schedule{nullptr};
  std::string last_state{""};

  void setup() override {
    //set_disabled_by_default(false);
    //set_icon("mdi:timer-outline");
    LOGV("setup(): %s", get_object_id().c_str());
  }
  
  void set_schedule(Schedule *_schedule) {
    schedule = _schedule;
  }

}; // CronNextSensor class


class CrontabText : public text::Text, public Component, public LoggerLocal<CrontabText> {
public:
  
  Schedule *schedule{nullptr};
  std::string last_state{""};

  void setup() override {
    //set_disabled_by_default(false);
    //set_icon("mdi:calendar-clock-outline");
    traits.set_min_length(0);
    traits.set_max_length(CRONTAB_MAX_LEN);
    traits.set_mode(text::TEXT_MODE_TEXT);
    LOGV("setup(): %s", get_object_id().c_str());
  }

  void set_schedule(Schedule *_schedule) {
    schedule = _schedule;
  }
  
  void control(const std::string &_state) {
    LOGV("CrontabText::control(): %d", &_state);
    schedule->setCrontab(_state);
  }
  
}; // CrontabText class


} // dynamic_cron namespace
} // esphome namespace

