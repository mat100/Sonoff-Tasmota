/*
  xdrv_91_ms_timers.ino - timer support for Sonoff-Tasmota

  Copyright (C) 2019  Matej Supik

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_MS_TIMERS
/*********************************************************************************************\
 * MS Timers
 *
 * Enable a timer using one or all of the following JSON values:
 * {"Enable":1,TimeON":"09:23",TimeOFF":"10:23",Days":"--TW--S","Repeat":1,"Output":1}
 *
 * Enable  0 = Off, 1 = On
 * TimeON  hours:minutes
 * TimeOFF hours:minutes
 * Days    7 day character mask starting with Sunday (SMTWTFS). 0 or - = Off, any other value = On
 * Repeat  0 = Execute once, 1 = Execute again
 * Output  1..16
 *
\*********************************************************************************************/

#define XDRV_91            91

#define D_CMND_MS_TIMER "Timer"
  #define D_JSON_MS_TIMER_ENABLE "Enable"
  #define D_JSON_MS_TIMER_TIME_ON "TimeON"
  #define D_JSON_MS_TIMER_TIME_OFF "TimeOFF"
  #define D_JSON_MS_TIMER_DAYS "Days"
  #define D_JSON_MS_TIMER_REPEAT "Repeat"
  #define D_JSON_MS_TIMER_OUTPUT "Output"
  #define D_JSON_MS_TIMER_NO_DEVICE "No GPIO as output configured"
#define D_CMND_MS_TIMERS "Timers"

const char kMSTimerCommands[] PROGMEM = D_CMND_MS_TIMER "|" D_CMND_MS_TIMERS;

uint16_t ms_timer_last_minute = 60;

byte ms_timer_output[MAX_RELAYS];
byte ms_timer_output_mem[MAX_RELAYS];

/*******************************************************************************************/

void MSTimerEverySecond(void)
{
  if (RtcTime.valid) {
    if (Settings.flag3.timers_enable && (uptime > 10) && (RtcTime.minute != ms_timer_last_minute)) {  // Execute from one minute after restart every minute only once
      ms_timer_last_minute = RtcTime.minute;
      int16_t time = (RtcTime.hour *60) + RtcTime.minute;
      uint8_t days = 1 << (RtcTime.day_of_week -1);

      for (byte i = 0; i < MAX_RELAYS; i++) ms_timer_output[i] = POWER_OFF;

      for (byte i = 0; i < (MAX_TIMERS / 2); i++) {
        Timer xtimer = Settings.timer[i];
        uint16_t set_time_on = xtimer.time;
        uint16_t set_time_off = Settings.timer[i+8].time;

        if (set_time_on < 0) { set_time_on = 0; }         // Stay today;
        if (set_time_on > 1439) { set_time_on = 1439; }
        if (set_time_off < 0) { set_time_off = 0; }
        if (set_time_off > 1439) { set_time_off = 1439; }

        if (xtimer.arm && (xtimer.days & days))
          if (set_time_off > set_time_on)
            if ((time >= set_time_on) && (time < set_time_off))
              ms_timer_output[xtimer.device +1] = POWER_ON;
            else if (ms_timer_output_mem[xtimer.device +1]) 
              Settings.timer[i].arm = xtimer.repeat;

          else if (set_time_off < set_time_on)
            if ((time >= set_time_on) || (time <= set_time_off))
              ms_timer_output[xtimer.device +1] = POWER_ON;
            else if (ms_timer_output_mem[xtimer.device +1]) 
              Settings.timer[i].arm = xtimer.repeat;
      }
    }
  } else
    for (byte i = 0; i < MAX_RELAYS; i++) ms_timer_output[i] = POWER_OFF; // Disable all outputs

  for (byte i = 0; i < MAX_RELAYS; i++)
    if (ms_timer_output_mem[i] != ms_timer_output[i]) {
      if (devices_present) ExecuteCommandPower(i, ms_timer_output[i], SRC_TIMER);    
      ms_timer_output_mem[i] = ms_timer_output[i];
    }
}

void PrepShowMSTimer(uint8_t index)
{
  char days[8] = { 0 };
  char sign[2] = { 0 };
  char soutput[80];

  Timer xtimer = Settings.timer[index -1];

  for (byte i = 0; i < 7; i++) {
    uint8_t mask = 1 << i;
    snprintf(days, sizeof(days), "%s%d", days, ((xtimer.days & mask) > 0));
  }

  soutput[0] = '\0';
  if (devices_present) {
    snprintf_P(soutput, sizeof(soutput), PSTR(",\"" D_JSON_MS_TIMER_OUTPUT "\":%d"), xtimer.device +1);
  }
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s\"" D_CMND_MS_TIMER "%d\":{\"" D_JSON_MS_TIMER_ENABLE "\":%d,\"" D_JSON_MS_TIMER_TIME_ON "\":\"%02d:%02d\",\"" D_JSON_MS_TIMER_TIME_OFF "\":\"%02d:%02d\",\"" D_JSON_MS_TIMER_DAYS "\":\"%s\",\"" D_JSON_MS_TIMER_REPEAT "\":%d%s}"),
    mqtt_data, index, xtimer.arm, xtimer.time / 60, xtimer.time % 60, Settings.timer[index -1 +8].time / 60, Settings.timer[index -1 +8].time % 60, days, xtimer.repeat, soutput);
}

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

boolean MSTimerCommand(void)
{
  char command[CMDSZ];
  char dataBufUc[XdrvMailbox.data_len];
  boolean serviced = true;
  uint8_t index = XdrvMailbox.index;

  UpperCase(dataBufUc, XdrvMailbox.data);
  int command_code = GetCommandCode(command, sizeof(command), XdrvMailbox.topic, kMSTimerCommands);
  if (-1 == command_code) {
    serviced = false;  // Unknown command
  }
  else if ((CMND_MS_TIMER == command_code) && (index > 0) && (index <= MAX_TIMERS)) {
    uint8_t error = 0;
    if (XdrvMailbox.data_len) {
      if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= MAX_TIMERS)) {
        if (XdrvMailbox.payload == 0) {
          Settings.timer[index -1].data = 0;  // Clear timer
        } else {
          Settings.timer[index -1].data = Settings.timer[XdrvMailbox.payload -1].data;  // Copy timer
        }
      } else {
        if (devices_present) {
          StaticJsonBuffer<256> jsonBuffer;
          JsonObject& root = jsonBuffer.parseObject(dataBufUc);
          if (!root.success()) {
            snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"" D_CMND_MS_TIMER "%d\":\"" D_JSON_INVALID_JSON "\"}"), index); // JSON decode failed
            error = 1;
          }
          else {
            char parm_uc[10];
            index--;
            if (root[UpperCase_P(parm_uc, PSTR(D_JSON_MS_TIMER_ENABLE))].success()) {
              Settings.timer[index].arm = (root[parm_uc] != 0);
            }
            if (root[UpperCase_P(parm_uc, PSTR(D_JSON_MS_TIMER_TIME_ON))].success()) {
              uint16_t itime = 0;
              int8_t value = 0;
              uint8_t sign = 0;
              char time_str[10];

              snprintf(time_str, sizeof(time_str), root[parm_uc]);
              const char *substr = strtok(time_str, ":");
              if (substr != NULL) {
                if (strchr(substr, '-')) {
                  sign = 1;
                  substr++;
                }
                value = atoi(substr);
                if (sign) { value += 12; }  // Allow entering timer offset from -11:59 to -00:01 converted to 12:01 to 23:59
                if (value > 23) { value = 23; }
                itime = value * 60;
                substr = strtok(NULL, ":");
                if (substr != NULL) {
                  value = atoi(substr);
                  if (value < 0) { value = 0; }
                  if (value > 59) { value = 59; }
                  itime += value;
                }
              }
              Settings.timer[index].time = itime;
            }
            if (root[UpperCase_P(parm_uc, PSTR(D_JSON_MS_TIMER_TIME_OFF))].success()) {
              uint16_t itime = 0;
              int8_t value = 0;
              uint8_t sign = 0;
              char time_str[10];

              snprintf(time_str, sizeof(time_str), root[parm_uc]);
              const char *substr = strtok(time_str, ":");
              if (substr != NULL) {
                if (strchr(substr, '-')) {
                  sign = 1;
                  substr++;
                }
                value = atoi(substr);
                if (sign) { value += 12; }  // Allow entering timer offset from -11:59 to -00:01 converted to 12:01 to 23:59
                if (value > 23) { value = 23; }
                itime = value * 60;
                substr = strtok(NULL, ":");
                if (substr != NULL) {
                  value = atoi(substr);
                  if (value < 0) { value = 0; }
                  if (value > 59) { value = 59; }
                  itime += value;
                }
              }
              Settings.timer[index +8].time = itime;
            }
            if (root[UpperCase_P(parm_uc, PSTR(D_JSON_MS_TIMER_DAYS))].success()) {
              // SMTWTFS = 1234567 = 0011001 = 00TW00S = --TW--S
              Settings.timer[index].days = 0;
              const char *tday = root[parm_uc];
              uint8_t i = 0;
              char ch = *tday++;
              while ((ch != '\0') && (i < 7)) {
                if (ch == '-') { ch = '0'; }
                uint8_t mask = 1 << i++;
                Settings.timer[index].days |= (ch == '0') ? 0 : mask;
                ch = *tday++;
              }
            }
            if (root[UpperCase_P(parm_uc, PSTR(D_JSON_MS_TIMER_REPEAT))].success()) {
              Settings.timer[index].repeat = (root[parm_uc] != 0);
            }
            if (root[UpperCase_P(parm_uc, PSTR(D_JSON_MS_TIMER_OUTPUT))].success()) {
              uint8_t device = ((uint8_t)root[parm_uc] -1) & 0x0F;
              Settings.timer[index].device = (device < devices_present) ? device : 0;
            }

            index++;
          }
        } else {
          snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"" D_CMND_MS_TIMER "%d\":\"" D_JSON_MS_TIMER_NO_DEVICE "\"}"), index);  // No outputs defined so nothing to control
          error = 1;
        }
      }
    }
    if (!error) {
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{"));
      PrepShowMSTimer(index);
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s}"), mqtt_data);
    }
  }
  else if (CMND_MS_TIMERS == command_code) {
    if (XdrvMailbox.data_len) {
      if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 1)) {
        Settings.flag3.timers_enable = XdrvMailbox.payload;
      }
      if (XdrvMailbox.payload == 2) {
        Settings.flag3.timers_enable = !Settings.flag3.timers_enable;
      }
    }

    snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_COMMAND_SVALUE, command, GetStateText(Settings.flag3.timers_enable));
    MqttPublishPrefixTopic_P(RESULT_OR_STAT, command);

    byte jsflg = 0;
    byte lines = 1;
    for (byte i = 0; i < MAX_TIMERS; i++) {
      if (!jsflg) {
        snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"" D_CMND_MS_TIMERS "%d\":{"), lines++);
      } else {
        snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,"), mqtt_data);
      }
      jsflg++;
      PrepShowMSTimer(i +1);
      if (jsflg > 3) {
        snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s}}"), mqtt_data);
        MqttPublishPrefixTopic_P(RESULT_OR_STAT, PSTR(D_CMND_MS_TIMERS));
        jsflg = 0;
      }
    }
    mqtt_data[0] = '\0';
  }
  else serviced = false;  // Unknown command

  return serviced;
}

/*********************************************************************************************\
 * Presentation
\*********************************************************************************************/

#ifdef USE_WEBSERVER

#define WEB_HANDLE_MS_TIMER "mst"

const char S_CONFIGURE_MS_TIMER[] PROGMEM = D_CONFIGURE_MS_TIMER;

const char HTTP_BTN_MENU_MS_TIMER[] PROGMEM =
  "<br/><form action='" WEB_HANDLE_MS_TIMER "' method='get'><button>" D_CONFIGURE_MS_TIMER "</button></form>";

const char HTTP_MS_TIMER_SCRIPT[] PROGMEM =
  "var pt=[],ct=99;"
  "function qs(s){"                                               // Alias to save code space
    "return document.querySelector(s);"
  "}"
  "function ce(i,q){"                                             // Create select option
    "var o=document.createElement('option');"
    "o.textContent=i;"
    "q.appendChild(o);"
  "}"
  "function st(){"                                                // Save parameters to hidden area
    "var i,l_on,l_off,n,p,s;"
    "m=0;s=0;"
    "n=1<<31;if(eb('a0').checked){s|=n;}"                         // Get arm
    "n=1<<15;if(eb('r0').checked){s|=n;}"                         // Get repeat
    "for(i=0;i<7;i++){n=1<<(16+i);if(eb('w'+i).checked){s|=n;}}"  // Get weekdays
    "i=qs('#d1').selectedIndex;if(i>=0){s|=(i<<23);}"             // Get output

    "l_on=((qs('#ho_on').selectedIndex*60)+qs('#mi_on').selectedIndex)&0x7FF;" // Get time on
    "pt[ct]= s | l_on;"
    "l_off=((qs('#ho_off').selectedIndex*60)+qs('#mi_off').selectedIndex)&0x7FF;" // Get time of
    "pt[ct+8]= s | l_off;"

    "eb('t0').value=pt.join();"                                   // Save parameters from array to hidden area
  "}"
  "function ot(t,e){"                                             // Select tab and update elements
    "var i,n,o,p,q,s;"
    "if(ct<99){st();}"                                            // Save changes
    "ct=t;"
    "o=document.getElementsByClassName('tl');"                    // Restore style to all tabs/buttons
    "for(i=0;i<o.length;i++){o[i].style.cssText=\"background-color:#ccc;color:#fff;font-weight:normal;\"}"
    "e.style.cssText=\"background-color:#fff;color:#000;font-weight:bold;\";"  // Change style to tab/button used to open content
    "s=pt[ct];"                                                   // Get parameters from array
    "p=s&0x7FF;"                                                  // Get time
    "q=Math.floor(p/60);if(q<10){q='0'+q;}qs('#ho_on').value=q;"     // Set hours on
    "q=p%60;if(q<10){q='0'+q;}qs('#mi_on').value=q;"                 // Set minutes on

    "s_off=pt[ct+8];"                                                // Get parameters from array (second index + 8)
    "p_off=s_off&0x7FF;"
    "q=Math.floor(p_off/60);if(q<10){q='0'+q;}qs('#ho_off').value=q;" // Set hours off
    "q=p_off%60;if(q<10){q='0'+q;}qs('#mi_off').value=q;"             // Set minutes off

    "for(i=0;i<7;i++){p=(s>>(16+i))&1;eb('w'+i).checked=p;}"      // Set weekdays
    "if(}1>0){"
      "p=(s>>23)&0xF;qs('#d1').value=p+1;"                        // Set output
    "}"
    "p=(s>>15)&1;eb('r0').checked=p;"                             // Set repeat
    "p=(s>>31)&1;eb('a0').checked=p;"                             // Set enable
  "}"
  "function it(){"                                                // Initialize elements and select first tab
    "var b,i,o,s;"
    "pt=eb('t0').value.split(',').map(Number);"                   // Get parameters from hidden area to array
    "s='';for(i=0;i<" STR(MAX_TIMERS/2) ";i++){b='';if(0==i){b=\" id='dP'\";}s+=\"<button type='button' class='tl' onclick='ot(\"+i+\",this)'\"+b+\">\"+(i+1)+\"</button>\"}"
    "eb('bt').innerHTML=s;"                                       // Create tabs                                              
    "eb('oa').innerHTML=\"<b>" D_MS_TIMER_OUTPUT "</b>&nbsp;<span><select style='width:60px;' id='d1' name='d1'></select></span>&emsp;\";" // Create Output

    "o=qs('#ho_on');for(i=0;i<=23;i++){ce((i<10)?('0'+i):i,o);}"  // Create hours on select options
    "o=qs('#mi_on');for(i=0;i<=59;i++){ce((i<10)?('0'+i):i,o);}"  // Create minutes on select options
    "o=qs('#ho_off');for(i=0;i<=23;i++){ce((i<10)?('0'+i):i,o);}" // Create hours off select options
    "o=qs('#mi_off');for(i=0;i<=59;i++){ce((i<10)?('0'+i):i,o);}" // Create minutes off select options

    "o=qs('#d1');for(i=0;i<}1;i++){ce(i+1,o);}"                   // Create outputs
    "var a='" D_DAY3LIST "';"
    "s='';for(i=0;i<7;i++){s+=\"<input style='width:5%;' id='w\"+i+\"' name='w\"+i+\"' type='checkbox'><b>\"+a.substring(i*3,(i*3)+3)+\"</b>\"}"
    "eb('ds').innerHTML=s;"                                       // Create weekdays
    "eb('dP').click();"                                           // Get the element with id='dP' and click on it
  "}";
const char HTTP_MS_TIMER_STYLE[] PROGMEM =
  ".tl{float:left;border-radius:0;border:1px solid #fff;padding:1px;width:6.25%;}"
  "</style>";
const char HTTP_FORM_MS_TIMER[] PROGMEM =
  "<fieldset style='min-width:470px;text-align:center;'>"
  "<legend style='text-align:left;'><b>&nbsp;" D_MS_TIMER_PARAMETERS "&nbsp;</b></legend>"
  "<form method='post' action='" WEB_HANDLE_MS_TIMER "' onsubmit='return st();'>"
  "<br/><input style='width:5%;' id='e0' name='e0' type='checkbox'{e0><b>" D_MS_TIMER_GLOBAL_ENABLE "</b><br/><br/><hr/>"
  "<input id='t0' name='t0' value='";
const char HTTP_FORM_MS_TIMER1[] PROGMEM =
  "' hidden><div id='bt' name='bt'></div><br/><br/><br/>"
  "<div id='oa' name='oa'></div><br/>"
  "<div>"
  "<input style='width:5%;' id='a0' name='a0' type='checkbox'><b>" D_MS_TIMER_ENABLE "</b>&emsp;"
  "<input style='width:5%;' id='r0' name='r0' type='checkbox'><b>" D_MS_TIMER_REPEAT "</b>"
  "</div><br/>"
  "<div>"
  "<b> " D_MS_TIMER_TIME_ON " </b>&nbsp;"
  "<span><select style='width:60px;' id='ho_on' name='ho_on'></select></span>"
  "&nbsp;" D_HOUR_MINUTE_SEPARATOR "&nbsp;"
  "<span><select style='width:60px;' id='mi_on' name='mi_on'></select></span>"
  "</div>"
  "<div>"
  "<b> " D_MS_TIMER_TIME_OFF " </b>&nbsp;"
  "<span><select style='width:60px;' id='ho_off' name='ho_off'></select></span>"
  "&nbsp;" D_HOUR_MINUTE_SEPARATOR "&nbsp;"
  "<span><select style='width:60px;' id='mi_off' name='mi_off'></select></span>"
  "</div>"
  "<br/>"
  "<div id='ds' name='ds'></div>";

void HandleMSTimerConfiguration(void)
{
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog_P(LOG_LEVEL_DEBUG, S_LOG_HTTP, S_CONFIGURE_MS_TIMER);

  if (WebServer->hasArg("save")) {
    MSTimerSaveSettings();
    HandleConfiguration();
    return;
  }

  String page = FPSTR(HTTP_HEAD);
  page.replace(F("{v}"), FPSTR(S_CONFIGURE_MS_TIMER));
  page += FPSTR(HTTP_MS_TIMER_SCRIPT);
  page += FPSTR(HTTP_HEAD_STYLE);
  page.replace(F("</style>"), FPSTR(HTTP_MS_TIMER_STYLE));
  page += FPSTR(HTTP_FORM_MS_TIMER);
  page.replace(F("{e0"), (Settings.flag3.timers_enable) ? F(" checked") : F(""));
  for (byte i = 0; i < MAX_TIMERS; i++) {
    if (i > 0) { page += F(","); }
    page += String(Settings.timer[i].data);
  }
  page += FPSTR(HTTP_FORM_MS_TIMER1);
  page.replace(F("}1"), String(devices_present));
  page += FPSTR(HTTP_FORM_END);
  page += F("<script>it();</script>");  // Init elements and select first tab/button
  page += FPSTR(HTTP_BTN_CONF);
  ShowPage(page);
}

void MSTimerSaveSettings(void)
{
  char tmp[MAX_TIMERS *12];  // Need space for MAX_TIMERS x 10 digit numbers separated by a comma
  Timer timer;

  Settings.flag3.timers_enable = WebServer->hasArg("e0");
  WebGetArg("t0", tmp, sizeof(tmp));
  char *p = tmp;
  snprintf_P(log_data, sizeof(log_data), PSTR(D_LOG_MQTT D_CMND_MS_TIMERS " %d"), Settings.flag3.timers_enable);
  for (byte i = 0; i < MAX_TIMERS; i++) {
    timer.data = strtol(p, &p, 10);
    p++;  // Skip comma
    if (timer.time < 1440) {
      bool flag = (timer.window != Settings.timer[i].window);
      Settings.timer[i].data = timer.data;
    }
    snprintf_P(log_data, sizeof(log_data), PSTR("%s,0x%08X"), log_data, Settings.timer[i].data);
  }
  AddLog(LOG_LEVEL_DEBUG);
}
#endif  // USE_WEBSERVER

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

boolean Xdrv91(byte function)
{
  boolean result = false;

  switch (function) {
    case FUNC_INIT:
      for (byte i = 0; i < (MAX_TIMERS / 2); i++) {
        Timer xtimer = Settings.timer[i];   
        if (xtimer.arm) {
          if (devices_present) { ExecuteCommandPower(xtimer.device +1, POWER_OFF, SRC_TIMER); }
        } 
      }
      break;
#ifdef USE_WEBSERVER
    case FUNC_WEB_ADD_BUTTON:
      if (devices_present) { strncat_P(mqtt_data, HTTP_BTN_MENU_MS_TIMER, sizeof(mqtt_data) - strlen(mqtt_data) -1); }
      break;
    case FUNC_WEB_ADD_HANDLER:
      WebServer->on("/" WEB_HANDLE_MS_TIMER, HandleMSTimerConfiguration);
      break;
#endif  // USE_WEBSERVER
    case FUNC_EVERY_SECOND:
      MSTimerEverySecond();
      break;
    case FUNC_COMMAND:
      result = MSTimerCommand();
      break;
  }
  return result;
}

#endif  // USE_MS_TIMER
