#ifndef BUILDING_NODE_EXTENSION
#define BUILDING_NODE_EXTENSION
#endif

#include "WebRtcConnection.h"
#include "ConnectionDescription.h"
#include "MediaStream.h"

#include <future>  // NOLINT

#include "lib/json.hpp"
#include "IOThreadPool.h"
#include "ThreadPool.h"

using v8::HandleScope;
using v8::Function;
using v8::FunctionTemplate;
using v8::Local;
using v8::Persistent;
using v8::Exception;
using v8::Value;
using json = nlohmann::json;

Nan::Persistent<Function> WebRtcConnection::constructor;

WebRtcConnection::WebRtcConnection() {
}

WebRtcConnection::~WebRtcConnection() {
  if (me.get() != nullptr) {
    me->setWebRtcConnectionEventListener(NULL);
  }
}

NAN_MODULE_INIT(WebRtcConnection::Init) {
  // Prepare constructor template
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("WebRtcConnection").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  // Prototype
  Nan::SetPrototypeMethod(tpl, "close", close);
  Nan::SetPrototypeMethod(tpl, "init", init);
  Nan::SetPrototypeMethod(tpl, "setRemoteDescription", setRemoteDescription);
  Nan::SetPrototypeMethod(tpl, "getLocalDescription", getLocalDescription);
  Nan::SetPrototypeMethod(tpl, "setRemoteSdp", setRemoteSdp);
  Nan::SetPrototypeMethod(tpl, "addRemoteCandidate", addRemoteCandidate);
  Nan::SetPrototypeMethod(tpl, "getLocalSdp", getLocalSdp);
  Nan::SetPrototypeMethod(tpl, "getCurrentState", getCurrentState);
  Nan::SetPrototypeMethod(tpl, "createOffer", createOffer);
  Nan::SetPrototypeMethod(tpl, "setMetadata", setMetadata);
  Nan::SetPrototypeMethod(tpl, "addMediaStream", addMediaStream);
  Nan::SetPrototypeMethod(tpl, "removeMediaStream", removeMediaStream);

  constructor.Reset(tpl->GetFunction());
  Nan::Set(target, Nan::New("WebRtcConnection").ToLocalChecked(), Nan::GetFunction(tpl).ToLocalChecked());
}


NAN_METHOD(WebRtcConnection::New) {
  if (info.Length() < 7) {
    Nan::ThrowError("Wrong number of arguments");
  }

  if (info.IsConstructCall()) {
    // Invoked as a constructor with 'new WebRTC()'
    ThreadPool* thread_pool = Nan::ObjectWrap::Unwrap<ThreadPool>(Nan::To<v8::Object>(info[0]).ToLocalChecked());
    IOThreadPool* io_thread_pool = Nan::ObjectWrap::Unwrap<IOThreadPool>(Nan::To<v8::Object>(info[1]).ToLocalChecked());
    v8::String::Utf8Value paramId(Nan::To<v8::String>(info[2]).ToLocalChecked());
    std::string wrtcId = std::string(*paramId);
    v8::String::Utf8Value param(Nan::To<v8::String>(info[3]).ToLocalChecked());
    std::string stunServer = std::string(*param);
    int stunPort = info[4]->IntegerValue();
    int minPort = info[5]->IntegerValue();
    int maxPort = info[6]->IntegerValue();
    bool trickle = (info[7]->ToBoolean())->BooleanValue();
    v8::String::Utf8Value json_param(Nan::To<v8::String>(info[8]).ToLocalChecked());
    bool use_nicer = (info[9]->ToBoolean())->BooleanValue();
    std::string media_config_string = std::string(*json_param);
    json media_config = json::parse(media_config_string);
    std::vector<erizo::RtpMap> rtp_mappings;
    if (media_config.find("rtpMappings") != media_config.end()) {
      json rtp_map_json = media_config["rtpMappings"];
      for (json::iterator it = rtp_map_json.begin(); it != rtp_map_json.end(); ++it) {
        erizo::RtpMap rtp_map;
        if (it.value()["payloadType"].is_number()) {
          rtp_map.payload_type = it.value()["payloadType"];
        } else {
          continue;
        }
        if (it.value()["encodingName"].is_string()) {
          rtp_map.encoding_name = it.value()["encodingName"];
        } else {
          continue;
        }
        if (it.value()["mediaType"].is_string()) {
          if (it.value()["mediaType"] == "video") {
            rtp_map.media_type = erizo::VIDEO_TYPE;
          } else if (it.value()["mediaType"] == "audio") {
            rtp_map.media_type = erizo::AUDIO_TYPE;
          } else {
            continue;
          }
        } else {
          continue;
        }
        if (it.value()["clockRate"].is_number()) {
          rtp_map.clock_rate = it.value()["clockRate"];
        }
        if (rtp_map.media_type == erizo::AUDIO_TYPE) {
          if (it.value()["channels"].is_number()) {
            rtp_map.channels = it.value()["channels"];
          }
        }
        if (it.value()["formatParameters"].is_object()) {
          json format_params_json = it.value()["formatParameters"];
          for (json::iterator params_it = format_params_json.begin();
              params_it != format_params_json.end(); ++params_it) {
            std::string value = params_it.value();
            std::string key = params_it.key();
            rtp_map.format_parameters.insert(rtp_map.format_parameters.begin(),
                std::pair<std::string, std::string> (key, value));
          }
        }
        if (it.value()["feedbackTypes"].is_array()) {
          json feedback_types_json = it.value()["feedbackTypes"];
          for (json::iterator feedback_it = feedback_types_json.begin();
              feedback_it != feedback_types_json.end(); ++feedback_it) {
              rtp_map.feedback_types.push_back(*feedback_it);
          }
        }
        rtp_mappings.push_back(rtp_map);
      }
    }

    std::vector<erizo::ExtMap> ext_mappings;
    unsigned int value = 0;
    if (media_config.find("extMappings") != media_config.end()) {
      json ext_map_json = media_config["extMappings"];
      for (json::iterator ext_map_it = ext_map_json.begin(); ext_map_it != ext_map_json.end(); ++ext_map_it) {
        ext_mappings.push_back({value++, *ext_map_it});
      }
    }

    erizo::IceConfig iceConfig;
    if (info.Length() == 15) {
      v8::String::Utf8Value param2(Nan::To<v8::String>(info[10]).ToLocalChecked());
      std::string turnServer = std::string(*param2);
      int turnPort = info[11]->IntegerValue();
      v8::String::Utf8Value param3(Nan::To<v8::String>(info[12]).ToLocalChecked());
      std::string turnUsername = std::string(*param3);
      v8::String::Utf8Value param4(Nan::To<v8::String>(info[13]).ToLocalChecked());
      std::string turnPass = std::string(*param4);
      v8::String::Utf8Value param5(Nan::To<v8::String>(info[14]).ToLocalChecked());
      std::string network_interface = std::string(*param5);

      iceConfig.turn_server = turnServer;
      iceConfig.turn_port = turnPort;
      iceConfig.turn_username = turnUsername;
      iceConfig.turn_pass = turnPass;
      iceConfig.network_interface = network_interface;
    }


    iceConfig.stun_server = stunServer;
    iceConfig.stun_port = stunPort;
    iceConfig.min_port = minPort;
    iceConfig.max_port = maxPort;
    iceConfig.should_trickle = trickle;
    iceConfig.use_nicer = use_nicer;

    std::shared_ptr<erizo::Worker> worker = thread_pool->me->getLessUsedWorker();
    std::shared_ptr<erizo::IOWorker> io_worker = io_thread_pool->me->getLessUsedIOWorker();

    WebRtcConnection* obj = new WebRtcConnection();
    obj->me = std::make_shared<erizo::WebRtcConnection>(worker, io_worker, wrtcId, iceConfig,
                                                        rtp_mappings, ext_mappings, obj);
    uv_async_init(uv_default_loop(), &obj->async_, &WebRtcConnection::eventsCallback);
    obj->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    // TODO(pedro) Check what happens here
  }
}

NAN_METHOD(WebRtcConnection::close) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  obj->me->setWebRtcConnectionEventListener(NULL);
  obj->me->close();
  obj->me.reset();

  if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&obj->async_))) {
    uv_close(reinterpret_cast<uv_handle_t*>(&obj->async_), NULL);
  }
}

NAN_METHOD(WebRtcConnection::init) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  std::shared_ptr<erizo::WebRtcConnection> me = obj->me;

  obj->eventCallback_ = new Nan::Callback(info[0].As<Function>());
  bool r = me->init();

  info.GetReturnValue().Set(Nan::New(r));
}

NAN_METHOD(WebRtcConnection::createOffer) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  std::shared_ptr<erizo::WebRtcConnection> me = obj->me;
  if (info.Length() < 3) {
    Nan::ThrowError("Wrong number of arguments");
  }
  bool video_enabled = info[0]->BooleanValue();
  bool audio_enabled = info[1]->BooleanValue();
  bool bundle = info[2]->BooleanValue();

  bool r = me->createOffer(video_enabled, audio_enabled, bundle);
  info.GetReturnValue().Set(Nan::New(r));
}

NAN_METHOD(WebRtcConnection::setMetadata) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  std::shared_ptr<erizo::WebRtcConnection> me = obj->me;

  v8::String::Utf8Value json_param(Nan::To<v8::String>(info[0]).ToLocalChecked());
  std::string metadata_string = std::string(*json_param);
  json metadata_json = json::parse(metadata_string);
  std::map<std::string, std::string> metadata;
  for (json::iterator item = metadata_json.begin(); item != metadata_json.end(); ++item) {
    std::string value = item.value().dump();
    if (item.value().is_object()) {
      value = "[object]";
    }
    if (item.value().is_string()) {
      value = item.value();
    }
    metadata[item.key()] = value;
  }

  me->setMetadata(metadata);

  info.GetReturnValue().Set(Nan::New(true));
}

NAN_METHOD(WebRtcConnection::setRemoteSdp) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  std::shared_ptr<erizo::WebRtcConnection> me = obj->me;

  v8::String::Utf8Value param(Nan::To<v8::String>(info[0]).ToLocalChecked());
  std::string sdp = std::string(*param);

  bool r = me->setRemoteSdp(sdp);

  info.GetReturnValue().Set(Nan::New(r));
}

NAN_METHOD(WebRtcConnection::setRemoteDescription) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  std::shared_ptr<erizo::WebRtcConnection> me = obj->me;

  ConnectionDescription* param =
    Nan::ObjectWrap::Unwrap<ConnectionDescription>(Nan::To<v8::Object>(info[0]).ToLocalChecked());
  auto sdp = std::shared_ptr<erizo::SdpInfo>(param->me);

  bool r = me->setRemoteSdpInfo(sdp);
  info.GetReturnValue().Set(Nan::New(r));
}

NAN_METHOD(WebRtcConnection::getLocalDescription) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  std::shared_ptr<erizo::WebRtcConnection> me = obj->me;

  std::shared_ptr<erizo::SdpInfo> sdp_info = me->getLocalSdpInfo();

  v8::Local<v8::Object> instance = ConnectionDescription::NewInstance();
  ConnectionDescription* description = ObjectWrap::Unwrap<ConnectionDescription>(instance);
  description->me = sdp_info;
  info.GetReturnValue().Set(instance);
}

NAN_METHOD(WebRtcConnection::addRemoteCandidate) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  std::shared_ptr<erizo::WebRtcConnection> me = obj->me;

  v8::String::Utf8Value param(Nan::To<v8::String>(info[0]).ToLocalChecked());
  std::string mid = std::string(*param);

  int sdpMLine = info[1]->IntegerValue();

  v8::String::Utf8Value param2(Nan::To<v8::String>(info[2]).ToLocalChecked());
  std::string sdp = std::string(*param2);

  bool r = me->addRemoteCandidate(mid, sdpMLine, sdp);

  info.GetReturnValue().Set(Nan::New(r));
}

NAN_METHOD(WebRtcConnection::getLocalSdp) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  std::shared_ptr<erizo::WebRtcConnection> me = obj->me;

  std::string sdp = me->getLocalSdp();

  info.GetReturnValue().Set(Nan::New(sdp.c_str()).ToLocalChecked());
}

NAN_METHOD(WebRtcConnection::getCurrentState) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  std::shared_ptr<erizo::WebRtcConnection> me = obj->me;

  int state = me->getCurrentState();

  info.GetReturnValue().Set(Nan::New(state));
}

NAN_METHOD(WebRtcConnection::addMediaStream) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  std::shared_ptr<erizo::WebRtcConnection> me = obj->me;

  MediaStream* param = Nan::ObjectWrap::Unwrap<MediaStream>(Nan::To<v8::Object>(info[0]).ToLocalChecked());
  auto wr = std::shared_ptr<erizo::MediaStream>(param->me);

  me->addMediaStream(wr);
}

NAN_METHOD(WebRtcConnection::removeMediaStream) {
  WebRtcConnection* obj = Nan::ObjectWrap::Unwrap<WebRtcConnection>(info.Holder());
  std::shared_ptr<erizo::WebRtcConnection> me = obj->me;

  v8::String::Utf8Value param(Nan::To<v8::String>(info[0]).ToLocalChecked());
  std::string streamId = std::string(*param);
  me->removeMediaStream(streamId);
}

// Async methods

void WebRtcConnection::notifyEvent(erizo::WebRTCEvent event, const std::string& message) {
  boost::mutex::scoped_lock lock(mutex);
  this->eventSts.push(event);
  this->eventMsgs.push(message);
  async_.data = this;
  uv_async_send(&async_);
}


NAUV_WORK_CB(WebRtcConnection::eventsCallback) {
  Nan::HandleScope scope;
  WebRtcConnection* obj = reinterpret_cast<WebRtcConnection*>(async->data);
  if (!obj || obj->me == NULL)
    return;
  boost::mutex::scoped_lock lock(obj->mutex);
  while (!obj->eventSts.empty()) {
    Local<Value> args[] = {Nan::New(obj->eventSts.front()), Nan::New(obj->eventMsgs.front().c_str()).ToLocalChecked()};
    Nan::MakeCallback(Nan::GetCurrentContext()->Global(), obj->eventCallback_->GetFunction(), 2, args);
    obj->eventMsgs.pop();
    obj->eventSts.pop();
  }
}
