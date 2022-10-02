/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2022 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "msm5232.h"
#include "../engine.h"
#include <math.h>

//#define rWrite(a,v) pendingWrites[a]=v;
#define rWrite(a,v) if (!skipRegisterWrites) {writes.emplace(a,v); if (dumpWrites) {addWrite(a,v);} }

#define NOTE_LINEAR(x) ((x)<<7)

const char* regCheatSheetMSM5232[]={
  "Select", "0",
  "MasterVol", "1",
  "FreqL", "2",
  "FreqH", "3",
  "DataCtl", "4",
  "ChanVol", "5",
  "WaveCtl", "6",
  "NoiseCtl", "7",
  "LFOFreq", "8",
  "LFOCtl", "9",
  NULL
};

const char** DivPlatformMSM5232::getRegisterSheet() {
  return regCheatSheetMSM5232;
}

void DivPlatformMSM5232::acquire(short* bufL, short* bufR, size_t start, size_t len) {
  for (size_t h=start; h<start+len; h++) {
    while (!writes.empty()) {
      QueuedWrite w=writes.front();
      msm->write(w.addr,w.val);
      regPool[w.addr&0x0f]=w.val;
      writes.pop();
    }
    memset(temp,0,16*sizeof(short));

    /*for (int i=0; i<8; i++) {
      oscBuf[i]->data[oscBuf[i]->needle++]=CLAMP((pce->channel[i].blip_prev_samp[0]+pce->channel[i].blip_prev_samp[1])<<1,-32768,32767);
    }*/

    msm->sound_stream_update(temp);
    
    //printf("tempL: %d tempR: %d\n",tempL,tempR);
    bufL[h]=0;
    for (int i=0; i<8; i++) {
      bufL[h]+=(temp[i]*partVolume[i])>>8;
    }
  }
}

const int attackMap[8]={
  0, 1, 2, 3, 4, 5, 5, 5
};

const int decayMap[16]={
  0, 1, 2, 3, 8, 9, 4, 10, 5, 11, 12, 13, 13, 13, 13, 13
};

void DivPlatformMSM5232::tick(bool sysTick) {
  for (int i=0; i<8; i++) {
    chan[i].std.next();
    if (chan[i].std.vol.had) {
      chan[i].outVol=VOL_SCALE_LINEAR(chan[i].vol&127,MIN(127,chan[i].std.vol.val),127);
    }
    if (chan[i].std.arp.had) {
      if (!chan[i].inPorta) {
        chan[i].baseFreq=NOTE_LINEAR(parent->calcArp(chan[i].note,chan[i].std.arp.val));
      }
      chan[i].freqChanged=true;
    }
    if (chan[i].std.duty.had) {
      groupControl[i>>2]=(chan[i].std.duty.val&0x1f)|(groupEnv[i>>2]?0x20:0);
      updateGroup[i>>2]=true;
    }
    if (chan[i].std.ex1.had) { // attack
      groupAR[i>>2]=attackMap[chan[i].std.ex1.val&7];
      updateGroupAR[i>>2]=true;
    }
    if (chan[i].std.ex2.had) { // decay
      groupDR[i>>2]=decayMap[chan[i].std.ex2.val&15];
      updateGroupDR[i>>2]=true;
    }
    if (chan[i].std.ex3.had) { // noise
      chan[i].noise=chan[i].std.ex3.val;
      chan[i].freqChanged=true;
    }
  }

  for (int i=0; i<2; i++) {
    if (updateGroup[i]) {
      rWrite(12+i,groupControl[i]);
      updateGroup[i]=false;
    }
    if (updateGroupAR[i]) {
      rWrite(8+i,groupAR[i]);
      updateGroupAR[i]=false;
    }
    if (updateGroupDR[i]) {
      rWrite(10+i,groupDR[i]);
      updateGroupDR[i]=false;
    }
  }

  for (int i=0; i<8; i++) {
    if (chan[i].freqChanged || chan[i].keyOn || chan[i].keyOff) {
      //DivInstrument* ins=parent->getIns(chan[i].ins,DIV_INS_PCE);
      chan[i].freq=chan[i].baseFreq+chan[i].pitch+chan[i].pitch2-(12<<7);
      if (chan[i].freq<0) chan[i].freq=0;
      if (chan[i].freq>0x2aff) chan[i].freq=0x2aff;
      if (chan[i].keyOn) {
        //rWrite(16+i*5,0x80);
        //chWrite(i,0x04,0x80|chan[i].vol);
      }
      if (chan[i].active) {
        rWrite(i,chan[i].noise?0xd8:(0x80|(chan[i].freq>>7)));
      }
      if (chan[i].keyOff) {
        rWrite(i,0);
      }
      if (chan[i].keyOn) chan[i].keyOn=false;
      if (chan[i].keyOff) chan[i].keyOff=false;
      chan[i].freqChanged=false;
    }
  }

  msm->set_vol_input(
    chan[0].active?((double)chan[0].outVol/127.0):0.0,
    chan[1].active?((double)chan[1].outVol/127.0):0.0,
    chan[2].active?((double)chan[2].outVol/127.0):0.0,
    chan[3].active?((double)chan[3].outVol/127.0):0.0,
    chan[4].active?((double)chan[4].outVol/127.0):0.0,
    chan[5].active?((double)chan[5].outVol/127.0):0.0,
    chan[6].active?((double)chan[6].outVol/127.0):0.0,
    chan[7].active?((double)chan[7].outVol/127.0):0.0
  );
}

int DivPlatformMSM5232::dispatch(DivCommand c) {
  switch (c.cmd) {
    case DIV_CMD_NOTE_ON: {
      DivInstrument* ins=parent->getIns(chan[c.chan].ins,DIV_INS_PCE);
      if (c.value!=DIV_NOTE_NULL) {
        chan[c.chan].baseFreq=NOTE_LINEAR(c.value);
        chan[c.chan].freqChanged=true;
        chan[c.chan].note=c.value;
      }
      chan[c.chan].active=true;
      chan[c.chan].keyOn=true;
      chan[c.chan].macroInit(ins);
      if (!parent->song.brokenOutVol && !chan[c.chan].std.vol.will) {
        chan[c.chan].outVol=chan[c.chan].vol;
      }
      chan[c.chan].insChanged=false;
      break;
    }
    case DIV_CMD_NOTE_OFF:
      chan[c.chan].active=false;
      chan[c.chan].keyOff=true;
      chan[c.chan].macroInit(NULL);
      break;
    case DIV_CMD_NOTE_OFF_ENV:
    case DIV_CMD_ENV_RELEASE:
      chan[c.chan].std.release();
      break;
    case DIV_CMD_INSTRUMENT:
      if (chan[c.chan].ins!=c.value || c.value2==1) {
        chan[c.chan].ins=c.value;
        chan[c.chan].insChanged=true;
      }
      break;
    case DIV_CMD_VOLUME:
      if (chan[c.chan].vol!=c.value) {
        chan[c.chan].vol=c.value;
        if (!chan[c.chan].std.vol.has) {
          chan[c.chan].outVol=c.value;
        }
      }
      break;
    case DIV_CMD_GET_VOLUME:
      if (chan[c.chan].std.vol.has) {
        return chan[c.chan].vol;
      }
      return chan[c.chan].outVol;
      break;
    case DIV_CMD_PITCH:
      chan[c.chan].pitch=c.value;
      chan[c.chan].freqChanged=true;
      break;
    case DIV_CMD_NOTE_PORTA: {
      int destFreq=NOTE_LINEAR(c.value2);
      bool return2=false;
      if (destFreq>chan[c.chan].baseFreq) {
        chan[c.chan].baseFreq+=c.value*parent->song.pitchSlideSpeed;
        if (chan[c.chan].baseFreq>=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      } else {
        chan[c.chan].baseFreq-=c.value*parent->song.pitchSlideSpeed;
        if (chan[c.chan].baseFreq<=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      }
      chan[c.chan].freqChanged=true;
      if (return2) {
        chan[c.chan].inPorta=false;
        return 2;
      }
      break;
    }
    case DIV_CMD_STD_NOISE_MODE:
      chan[c.chan].noise=c.value;
      chan[c.chan].freqChanged=true;
      break;
    case DIV_CMD_LEGATO:
      chan[c.chan].baseFreq=NOTE_LINEAR(c.value+((chan[c.chan].std.arp.will && !chan[c.chan].std.arp.mode)?(chan[c.chan].std.arp.val):(0)));
      chan[c.chan].freqChanged=true;
      chan[c.chan].note=c.value;
      break;
    case DIV_CMD_PRE_PORTA:
      if (chan[c.chan].active && c.value2) {
        if (parent->song.resetMacroOnPorta) chan[c.chan].macroInit(parent->getIns(chan[c.chan].ins,DIV_INS_PCE));
      }
      if (!chan[c.chan].inPorta && c.value && !parent->song.brokenPortaArp && chan[c.chan].std.arp.will) chan[c.chan].baseFreq=NOTE_LINEAR(chan[c.chan].note);
      chan[c.chan].inPorta=c.value;
      break;
    case DIV_CMD_GET_VOLMAX:
      return 127;
      break;
    case DIV_ALWAYS_SET_VOLUME:
      return 1;
      break;
    default:
      break;
  }
  return 1;
}

void DivPlatformMSM5232::muteChannel(int ch, bool mute) {
  isMuted[ch]=mute;
  msm->mute(ch,mute);
}

void DivPlatformMSM5232::forceIns() {
  for (int i=0; i<8; i++) {
    chan[i].insChanged=true;
    chan[i].freqChanged=true;
  }
  for (int i=0; i<2; i++) {
    updateGroup[i]=true;
    updateGroupAR[i]=true;
    updateGroupDR[i]=true;
  }
}

void* DivPlatformMSM5232::getChanState(int ch) {
  return &chan[ch];
}

DivMacroInt* DivPlatformMSM5232::getChanMacroInt(int ch) {
  return &chan[ch].std;
}

DivDispatchOscBuffer* DivPlatformMSM5232::getOscBuffer(int ch) {
  return oscBuf[ch];
}

unsigned char* DivPlatformMSM5232::getRegisterPool() {
  return regPool;
}

int DivPlatformMSM5232::getRegisterPoolSize() {
  return 14;
}

void DivPlatformMSM5232::reset() {
  while (!writes.empty()) writes.pop();
  memset(regPool,0,128);
  for (int i=0; i<8; i++) {
    chan[i]=DivPlatformMSM5232::Channel();
    chan[i].std.setEngine(parent);
  }
  if (dumpWrites) {
    addWrite(0xffffffff,0);
  }
  msm->device_start();
  msm->device_reset();
  memset(temp,0,16*sizeof(short));
  cycles=0;
  curChan=-1;
  delay=500;

  for (int i=0; i<2; i++) {
    groupControl[i]=15|(groupEnv[i]?0x20:0);
    groupAR[i]=0;
    groupDR[i]=5;

    updateGroup[i]=true;
    updateGroupAR[i]=true;
    updateGroupDR[i]=true;
  }

  for (int i=0; i<8; i++) {
    rWrite(i,0);
    partVolume[i]=initPartVolume[i];
    msm->mute(i,isMuted[i]);
  }
}

bool DivPlatformMSM5232::isStereo() {
  return false;
}

bool DivPlatformMSM5232::keyOffAffectsArp(int ch) {
  return true;
}

void DivPlatformMSM5232::notifyInsDeletion(void* ins) {
  for (int i=0; i<8; i++) {
    chan[i].std.notifyInsDeletion((DivInstrument*)ins);
  }
}

void DivPlatformMSM5232::setFlags(const DivConfig& flags) {
  chipClock=2119040;
  detune=flags.getInt("detune",0);
  msm->set_clock(chipClock+detune*1024);
  rate=msm->get_rate();
  for (int i=0; i<8; i++) {
    oscBuf[i]->rate=rate;
  }
  initPartVolume[0]=flags.getInt("partVolume0",255);
  initPartVolume[1]=flags.getInt("partVolume1",255);
  initPartVolume[2]=flags.getInt("partVolume2",255);
  initPartVolume[3]=flags.getInt("partVolume3",255);
  initPartVolume[4]=flags.getInt("partVolume4",255);
  initPartVolume[5]=flags.getInt("partVolume5",255);
  initPartVolume[6]=flags.getInt("partVolume6",255);
  initPartVolume[7]=flags.getInt("partVolume7",255);

  capacitance[0]=flags.getFloat("capValue0",390.0f);
  capacitance[1]=flags.getFloat("capValue1",390.0f);
  capacitance[2]=flags.getFloat("capValue2",390.0f);
  capacitance[3]=flags.getFloat("capValue3",390.0f);
  capacitance[4]=flags.getFloat("capValue4",390.0f);
  capacitance[5]=flags.getFloat("capValue5",390.0f);
  capacitance[6]=flags.getFloat("capValue6",390.0f);
  capacitance[7]=flags.getFloat("capValue7",390.0f);

  groupEnv[0]=flags.getBool("groupEnv0",true);
  groupEnv[1]=flags.getBool("groupEnv1",true);
  
  msm->set_capacitors(
    capacitance[0]*0.000000001,
    capacitance[1]*0.000000001,
    capacitance[2]*0.000000001,
    capacitance[3]*0.000000001,
    capacitance[4]*0.000000001,
    capacitance[5]*0.000000001,
    capacitance[6]*0.000000001,
    capacitance[7]*0.000000001
  );
}

void DivPlatformMSM5232::poke(unsigned int addr, unsigned short val) {
  rWrite(addr,val);
}

void DivPlatformMSM5232::poke(std::vector<DivRegWrite>& wlist) {
  for (DivRegWrite& i: wlist) rWrite(i.addr,i.val);
}

int DivPlatformMSM5232::init(DivEngine* p, int channels, int sugRate, const DivConfig& flags) {
  parent=p;
  dumpWrites=false;
  skipRegisterWrites=false;
  for (int i=0; i<8; i++) {
    isMuted[i]=false;
    oscBuf[i]=new DivDispatchOscBuffer;
  }
  msm=new msm5232_device(2119040);
  msm->device_start();
  setFlags(flags);
  reset();
  return 8;
}

void DivPlatformMSM5232::quit() {
  for (int i=0; i<8; i++) {
    delete oscBuf[i];
  }
  if (msm!=NULL) {
    msm->device_stop();
    delete msm;
    msm=NULL;
  }
}

DivPlatformMSM5232::~DivPlatformMSM5232() {
}