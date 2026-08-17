#pragma once



#include <Arduino.h>



#include "Config.h"



// Finish Setup stage isolation — [finish-stage] logs only. Remove after root cause proven.

namespace FinishTrace {



class StageScope {

 public:

  explicit StageScope(const char *name);

  ~StageScope();

  void setSuccess(bool ok);

  void fail();



 private:

  const char *_name;

  uint32_t    _beginMs;

  bool        _success;

};



void enterPipeline();

void exitPipeline();

bool pipelineActive();

void jobLifecycle(uint32_t jobId, const char *transition);

const char *currentStage();



// Blocking operation wait diagnostics — [finish-op] prefix.

struct BlockingOpConfig {

  const char *name;

  const char *waitingFor;

  uint32_t    timeoutMs;

  uint32_t    retryCount;

  const char *reason;

  const char *state;

};



class BlockingOpScope {

 public:

  explicit BlockingOpScope(const BlockingOpConfig &cfg);

  explicit BlockingOpScope(const char *name);  // inactive if empty or !pipelineActive

  ~BlockingOpScope();

  void fail(const char *error = nullptr);

  void setRetry(uint32_t count);

  void setState(const char *state);

  void setWaitingFor(const char *waitingFor);



  static void updateActiveState(const char *state, const char *waitingFor = nullptr);

  static void setActiveRetry(uint32_t count);

  static bool hasActiveBlockingOp();



 private:

  bool        _active;

  bool        _ownedSlot;

  int         _stackIndex;

  bool        _success;

  const char *_error;

};



// Lightweight op markers for non-blocking grouping (no WAITING heartbeats).

class OpScope {

 public:

  explicit OpScope(const char *name);

  ~OpScope();

  void fail();



 private:

  const char *_name;

  uint32_t    _beginMs;

  bool        _success;

  bool        _active;

};



void opEvent(const char *msg);

void opReturn(const char *context, bool success);



void portalHttpGetReceived(const char *filename);

void portalHttpResponseCompleted(const char *filename);



// Factory helpers for finish-pipeline blocking ops.

BlockingOpConfig routerApiOp(const char *name, const char *reason = nullptr);

BlockingOpConfig tcpConnectOp(const char *name);

BlockingOpConfig storageWriteOp(const char *name, const char *media);

BlockingOpConfig storageReadOp(const char *name, const char *media);

BlockingOpConfig fixedDelayOp(const char *name, uint32_t delayMs);

BlockingOpConfig portalFetchOp(const char *name);



}  // namespace FinishTrace

