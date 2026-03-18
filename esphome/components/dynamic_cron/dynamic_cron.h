
#pragma once

#include <ccronexpr.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <ctime> // c++ time package
#include <regex>
#include <vector>
#include <algorithm>
#include <time.h> // C time package
#include "version.h"
#include "logger_local.h"


namespace esphome {
namespace dynamic_cron {

// class Schedule;
// class SchedulePrefs;

// This is the timestamp of the firmware build.
// This will be set in python and is seconds from epoch.
//std::time_t TIMESTAMP;
//extern std::time_t TIMESTAMP;
inline std::time_t TIMESTAMP = 1234567890;

inline std::string TIME_FORMAT = "%Y-%m-%d %H:%M:%S";

void printVersion() {
  printf("DynamicCron version: %s, firmware build: %lld\n", VERSION.c_str(), (long long)TIMESTAMP);
  // Logging is not yet set up here.
  //ESP_LOGI("DynamicCron", "version: %s, firmware build: %lld\n", VERSION.c_str(), (long long)TIMESTAMP);
}


// Core definition of the schedule object.
class ScheduleCore : public LoggerLocal<ScheduleCore> {
  
protected:
  
  // Basic data points.
  std::string   schedule_name;
  std::string   schedule_id;
  std::string   crontab;
  std::time_t   next_expiry;
  bool          bypass;
  bool          remember_next;
  std::string   id_hash;
  std::string   bad_cron_expr;
  std::string   time_format;
  
  // Lamba for call to target action.
  // Can also receive basic function pointer.
  // Can NOT take lambda captures.
  // See the Schedule constructor (in dynamic_cron_esphome.h).
  //
  // Update: I don't think there's any way to pass just a function pointer
  // to esphome through the yaml config lambda fields, so restricting
  // this to not accept captures adds no benefit to esphome users.
  // (even if it's helpful in the isolated context of this class alone).
  // Maybe we can overload the constructor to handle lambdas with/without captures,
  // and then make the lambda-build in python pass in the captures '[=]'.
  // See here for discussion of captures in esphome: https://github.com/esphome/issues/issues/249
  bool(*target_action_fptr)();

  // These default fields are what hold the user input from the yaml config in esphome.
  // So if user runtime settings get lost or botched, their setup will always revert
  // to their configured defaults. See the setXxxDefault() methods below.
  
  std::string   crontab_default;
  bool          bypass_default;
  bool          remember_next_default;
  
    
public:
  
  // self reference, allows LoggerLocal to reference schedule members.
  ScheduleCore* schedule;
  
  // Custom constructor method to create ScheduleCore object.
  // NOTE: The function-pointer argument must have NO captures, if it's receiving a lambda.
  // Otherwise, the lambda won't be converted to a simple function/pointer.
  // So, if you pass in a lambda, the [] must be empty.
  //
  ScheduleCore( // schedule-name, schedule-id, target-action-lambda-or-function-pointer
    std::string _name,
    std::string _id,
    bool(*_target_action_fptr)()
  ) :
    schedule(this),
    schedule_name(_name),
    schedule_id(_id),
    crontab(""),
    crontab_default(""),
    next_expiry(0),
    bypass(false),
    bypass_default(false),
    remember_next(false),
    remember_next_default(false),
    target_action_fptr(_target_action_fptr),
    id_hash(""),
    time_format(TIME_FORMAT)
  {
    id_hash = GetHash(schedule_id);
    LOGV("Initializing ScheduleCore object %s %s", _id.c_str(), id_hash.c_str());
    
    // These all work! ... but not from timeToString() method. Update: that may have been fixed?
    LOGV("time_format: %s", getTimeFormat());
    LOGV("TIME_FORMAT: %s", TIME_FORMAT.c_str());
    LOGV("EQUAL? %d", (TIME_FORMAT == time_format));
    LOGV("id_hash: %s", id_hash.c_str());
    
    AddToSchedules(this);
    
  } // end ScheduleCore(...).
  
  
  // Globally accessible wrapper for access to all_schedules static var.
  // Are we still using this? YES
  static std::vector<ScheduleCore*>& Schedules() {
      static std::vector<ScheduleCore*> all_schedules;
      return all_schedules;
  }
  
  
  // Gets indexed member of Schedules.
  // Are we still using this? Not internally, but maybe from esphome config?
  static ScheduleCore* Schedules(int index) {
    if (!Schedules().empty()) {
      return Schedules()[index];
    }
    else {
      return nullptr;
    }
  }
  
  
  // Gets item of Schedules vector by schedule_id (as if Schedules was a map).
  // Are we still using this? Not internally, but maybe from esphome config?
  static ScheduleCore* Schedules(std::string _id) {
    if (!Schedules().empty()) {
      for (auto& x : Schedules()) {
        //if (x->schedule_id == _id) { // compares pointers, which might be different even if value matches.
        //if (std::strcmp(x->schedule_id, _id) == 0) { // strcmp() is for char* strings.
        if (x->schedule_id == _id) {
          return x;
        }
      }
    }
    return nullptr;
  }


  std::string getName() {
    return schedule_name;
  }
 
  std::string getId() {
    return schedule_id;
  }
 

  // Gets human-readable time of next_expiry field (not the calc).
  //
  std::string cronNextString(std::string _default="") {
    if (next_expiry == 0) {
      //std::string str(_default);
      //return str;
      //if (bad_cron_expr != "" && bypass == 0) {
      if (bad_cron_expr.length() > 0 && !bypass) {
        return bad_cron_expr;
      }
      else {
        return _default;
      }
    }
    else {
      return timeToString(next_expiry);
    }
  }


  // Getter for next_expiry time_t field.
  std::time_t getNextExpiry() {
    return next_expiry;
  }


  // Sets next_expiry time_t from crontab field.
  //
  virtual void setNextExpiry() {
    if (!timeIsValid()) {  // If system time is not valid, skip all of this.
      LOGW("Set next_expiry failed, crontab: %s, bypass: %d, remember: %d, now: %lld",
            crontab.c_str(),
            bypass,
            remember_next,
            (long long)timeNow()
      );
      return;
    }
    LOGV("setNextExpiry() --> timeIsValid(): TRUE");
    if (crontab.length() == 0 || bypass) {
      next_expiry = 0;
    }
    else {
      next_expiry = calcNextExpiry();
    }
    LOGD("Set next_expiry vars, crontab: %s, bypass: %d, remember: %d, now: %lld, %s",
          crontab.c_str(),
          bypass,
          remember_next,
          (long long)timeNow(),
          timeToString(timeNow()).c_str()
    );
    LOGI("Set next_expiry [%lld, %s]", (long long)next_expiry, timeToString(next_expiry).c_str());
  }


  // Experimental overload sets next_expiry from user input time_t.
  // The design logic was: if input is valid-time, ! bypass, > now, < calcNextExpiry(), then next_expiry=input;
  // however it might not be exactly that in the code.
  //
  // This is not currently used.
  //
  // To get time_t from user input string, use:
  // 
  //   std::time_t parsed = stringToTime(input);
  //
  void setNextExpiry(std::time_t input) {
    if (bypass)
        return;
    if (
      timeIsValid() &&
      timeIsValid(input) &&
      difftime(input, timeNow()) > 0 &&
      difftime(calcNextExpiry(), input) > 0
      // Why does input need to be < calcNextExpiry()?
      // It allows a one-off run, while still maintaining a legit crontab schedule.
      // If no crontab exists, then input can be any time in the future. In that case,
      // we need to make sure to clear out the manual next_expiry after it's used,
      // otherwise it'll trigger with every loop.
    ){
      next_expiry = input;

      LOGI("Setting next_expiry with input [%lld, %s]",
        (long long)input,
        timeToString(input).c_str()
      );
    }
    else {
      LOGW("setNextExpiry(user-input) invalid input or current-time [%lld, %s]",
        (long long)input,
        timeToString(input).c_str()
      );

      setNextExpiry();
    }
  }


  // Gets string from crontab field.
  //std::string getCrontab() { // returns copy of crontab.
  // This returns a reference to crontab and is more efficient.
  const std::string& getCrontab() const {
    return crontab;
  }


  // Sets crontab with given string.
  virtual std::string setCrontab(std::string str) {
    crontab = str;
    LOGI("Set crontab '%s'", crontab.c_str());
    setNextExpiry();
    return crontab;
  }


  // Gets bypass bool field.
  bool getBypass() {
    return bypass;
  }


  // Sets bypass bool field and resets cron next accordingly.
  virtual bool setBypass(bool val) {
    bypass = val;
    LOGI("Set bypass '%d'", bypass);
    setNextExpiry();
    return val;
  }

  // Gets remember_next bool field.
  bool getRememberNext() {
    return remember_next;
  }


  // Sets remember_next bool field.
  virtual bool setRememberNext(bool val) {
    remember_next = val;
    LOGI("Set remember-next '%d'", remember_next);
    return val;
  }
  
  
  // Gets schedule_id string field.
  std::string getIdString() {
    return schedule_id;
  }
  
  
  // Gets schedule_name string field.
  std::string getNameString() {
    return schedule_name;
  }
  
  
  const char* getTimeFormat() {
    return time_format.c_str();
  }
  
  
  // These default fields are what hold the user input from the yaml config in esphome.
  // They are mainly used in get/set preference field operations.
  //
  // So if user runtime settings get lost or botched, their setup will always revert
  // to their configured defaults.
  
  void setBypassDefault(bool val) {
    bypass_default = val;
  }
  
  
  void setRememberNextDefault(bool val) {
    remember_next_default = val;
  }
  
  
  void setCrontabDefault(std::string val) {
    crontab_default = val;
  }
  
  void setTimeFormatDefault(std::string val = TIME_FORMAT) {
    // Since we don't currently give the user an API for the time_format field at runtime,
    // This only needs to set the main time_format field. If we give the user a runtime
    // time_format input, we'll need to create and use a time_format_default field.
    // 
    if (val != "") {
      time_format = val;
    }
  }


  // Builds human-readable string from time_t.
  // See here for printing time_t data:
  //   https://stackoverflow.com/questions/18422384/how-to-print-time-t-in-a-specific-format
  static std::string timeToFormattedString(std::time_t timet, std::string _format = TIME_FORMAT) {
    // I disabled the timeIsValid() check here to prevent circular definition,
    // since I want to use timeToString() in the timeIsValid() funcion.
    // If we need to re-activate timeIsValid() here, remove timeToString() from timeIsValid().
    //
    if (timet != 0) {   //timeIsValid()) {
      struct tm * timetm;
      // Converts time_t to tm (a fancy time object), cuz that's what strftime wants.
      timetm = localtime(&timet);
      char str[24];
      strftime(str, sizeof(str), _format.c_str(), timetm);
      
      //SLOGVV("dynamic_cron", "From inside timeToString() '%s'", str);
      return (std::string)str;
    }
    else {
      return "";
    }
  }
  
  // Instance-specific wrapper for static method timeToFormattedString().
  std::string timeToString(std::time_t timet = std::time(NULL)) {
    return timeToFormattedString(timet, time_format);
  }
  
  
  std::time_t stringToTime(std::string input) {
    //std::string timeString = "2024-09-28 16:25:00"; // Example time string
  
    // Create a tm struct to store the parsed time
    struct tm tm_struct = {};
  
    // Parse the time string using strptime
    if (strptime(input.c_str(), "%Y-%m-%d %H:%M:%S", &tm_struct) == nullptr) {
        LOGE("Error parsing time string '%s'", input.c_str());
        return 0;
    }
  
    // Convert the tm struct to a time_t value
    time_t t_time = mktime(&tm_struct);
  
    // Log the time_t value
    LOGV("stringToTime() parsed time '%s' in seconds since epoch: %lld", input.c_str(), (long long)t_time);
    // Log the reverse operation.
    LOGV("stringToTime() reverse operation: %s", timeToString(t_time).c_str());
  
    return t_time;
  }
  
  
protected:

  // Adds a schedule object to a globally accessible vector array 'all_schedules'.
  // Are we still using this?
  static void AddToSchedules(ScheduleCore* _schedule) {
      SLOGV(LOGTAG, "Adding Schedule '%s' %s to Schedules vector",
        _schedule->schedule_name.c_str(),
        _schedule->schedule_id.c_str()
      );
      
      Schedules().push_back(_schedule);
  }
  
  // Performs the target action.
  void cronAction() {
    LOGI("%s cron schedule calling action(s)", schedule_name.c_str());
    bool result = target_action_fptr();
    if (result)
      setNextExpiry();
  }


  // Gets next time_t, given cron expression(s) string in crontab.
  std::time_t calcNextExpiry(std::string _crontab = "", std::time_t ref_time = 0) {
    if (_crontab.length() == 0){ _crontab = crontab; }
    if (ref_time == 0) { ref_time = timeNow(); }

    // Returns 0 if no crontab or ref_time.
    //if (_crontab == "" || ref_time == 0) { return 0; }
    if (_crontab.length() == 0 || ref_time == 0) { return 0; }

    // Requests sorted vector of nexts given crontab parsing string regex.
    std::string regex_str = " *\\| *";
    auto nexts = vectorOfNext(splitString(_crontab, regex_str), ref_time);

    // Logs next-run for each crontab.
    // for (auto& item: nexts)
    // {
    //   LOGD("Sorted cron-next: %s", timeToString(item).c_str());
    // }

    // Returns first (soonest) time_t from vector-of-nexts.
    return nexts[0];
  }


  // Returns current time as time_t.
  // TODO: Do we really need this?
  // I guess it's a way to encapsulate a standard way of getting current time.
  std::time_t timeNow() {
    return std::time(NULL);
  }


  // Is current (or given) time valid (synced & legit)?
  // Even if it's a valid system time, it must be within a reasonable range,
  // so it can't be 0 (1969, 1970, something like that, depending on locale).
  // We check with ESPHome, if it's loaded, but we always fall back to core system time.
  //
  //
  // NOTE: All callable log lines in this method could run many
  //       times per second, if conditions permit. Only enable
  //       them if necessary for debugging.
  //
  bool timeIsValid(std::time_t now = std::time(NULL)) {
    return (std::difftime(now, TIMESTAMP) >= 0);
  }


  // Function to split std::string on regex.
  std::vector<std::string> splitString(const std::string str, const std::string regex_str) {
      std::regex regexz(regex_str);
      return {std::sregex_token_iterator(str.begin(), str.end(), regexz, -1),
              std::sregex_token_iterator()};
  }


  // Returns sorted vector of next time_t values for given vector-of-crontab-strings,
  // with one soonest next-time value from each crontab expresion.
  // The first value in the returned vector is soonest next-time of all the given crontab expressions.
  //
  std::vector<std::time_t> vectorOfNext(std::vector<std::string> crontabs, std::time_t ref_time = 0) {
    if (ref_time == 0) { ref_time = timeNow(); }
    std::vector<std::time_t> start_times;

    for (auto& item: crontabs)
    {
      cron_expr expr;
      const char *err = 0;
      cron_parse_expr(item.c_str(), &expr, &err);
      if (err != 0) {
        LOGW("Not a valid cron expression '%s' %s", item.c_str(), err);
        
        bad_cron_expr = "'";
        bad_cron_expr += item;
        bad_cron_expr += "' ";
        bad_cron_expr += err;
        
        return {(std::time_t)0};
      }
      std::time_t next = cron_next(&expr, ref_time);
      if (next == -1)
        return {(std::time_t)0};

      start_times.push_back(next);
    }
    
    bad_cron_expr = "";

    // Sorts (in-place) vector of start_times values from soonest to furthest.
    std::sort(start_times.begin(), start_times.end(), [ref_time](std::time_t& a, std::time_t& b)
      { 
        return std::difftime(a, b) < 0;
      }
    );  

    return start_times;
  }
  
  
  // Creates a hash from a string.
  static std::string GetHash(std::string input, int len = 15) {
    //const std::string input = _input;
    const std::hash<std::string> hasher;
    const auto hashResult = hasher(input);
    
    // Convert to hex string
    std::stringstream stream;
    stream << std::hex << hashResult;
    std::string result( stream.str() );

    // Output substring of the hex string.
    //std::cout<<result;
    return (("H"+ result).substr(0,len));
  }

  
}; // ScheduleCore class


} // dynamic_cron namespace
} // esphome namespace

