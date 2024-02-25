// -*- mode: c++ -*-
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cerrno>
#include <time.h>

#include "GlobalHooks.h"

#define CIODIR "/mnt/T2TMP"
//#define CIODIR "."
#define CINPUTFILE (CIODIR "/input.dat")
#define COUTPUTFILE (CIODIR "/output.dat")
#define CTAGSFILE (CIODIR "/tags.dat")
#define CONFIGFILE (CIODIR "/config.dat")

#define SNAPSHOTDIRKEYWORD "snapshotdir"

#define USLEEPTIME 20000u  // 20ms, 50Hz

#define INHIBITIONBASE 102
#define INHIBITIONMIN 2

#define __FILENAME__ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)
#define FLMSG(str) fprintf(stderr,"%s:%d: %s\n",__FILENAME__, __LINE__, str)
#define FLMARK() FLMSG("mark")

inline BrainStemSupport::BrainStemSupport()
  : _cfgCount(0)
  , _snapshotCfgIndex(-1)
  , _bufferLen(0u)
  , _updates(0u)
  , _routed(0u)
  , _tagCount(0u)
  , _bvcodeVal(0u)
  , _sensesRead(false)
{
  if (fstat(0,&_istat) < 0) fdie("stating","stdin"); // just to have something
  _rstat = _istat;
  _tstat = _istat;
  _cstat = _istat;
}

inline bool BrainStemSupport::open()
{
  if (!tryLoadConfig() ||
      !tryLoadTags() ||
      !readInputFile())
    return false;
  if (_sensesRead)
    requestSnapshot();
  return true;
}

inline bool BrainStemSupport::close()
{
  writeOutputFile();
  return true;
}

inline bool BrainStemSupport::tryLoadConfig() { //< true if config.dat loaded okay (now or previously)
  //FLMARK();
  //fprintf(stderr,"%s\n",__PRETTY_FUNCTION__);
  bool ret = false;
  int fd = ::open(CONFIGFILE, O_RDONLY);
  if (fd < 0) return ret;
  //FLMARK();

  struct stat nowstat;
  if (fstat(fd,&nowstat) < 0) fdie("stating",CONFIGFILE);
    
  if (_cfgCount == 0u || diffMTime(nowstat,_cstat)) { // if no or new config, load
    readConfig(fd);
    _cstat = nowstat;
  }

  ret = true;
  //FLMARK();
  ::close(fd);
  return ret;
}

// <0: eof/err before start (strlen(dest)==0),
// ==0: eof or max before delim (dest[] maybe incomplete, but null terminated),
// >0: good read
inline int BrainStemSupport::readThrough(int fd, char delim, char * dest, unsigned max)
{
  if (dest == 0 || max == 0) return -2;
  unsigned numread = 0;
  int ret = 2; // flag, >1
  while (1) {
    char c;

    if (1 != read(fd,&c,1)) ret = (numread==0) ? -1 : 0;
    else if (numread == max-1) ret = 0;
    else if (c == delim) ret = 1;
    
    if (ret == 2) dest[numread++] = c;
    else {
      dest[numread] = 0;
      break;
    }
  }
  return ret;
}

inline bool BrainStemSupport::readConfig(int fd) {
  memset(&_cfgInfo,0,sizeof(CfgInfo));
    
  _cfgCount = 0u;
  _snapshotCfgIndex = -1;
  int ret;
  while (true) {
    ret = readThrough(fd,':',_cfgInfo[_cfgCount]._cfgName,MAXCFGNAMELEN);
    if (ret <= 0) return ret<0;
    ret = readThrough(fd,'\n',_cfgInfo[_cfgCount]._cfgValue,MAXCFGVALUELEN);
    if (ret <= 0) return false;
    if (!strcmp(SNAPSHOTDIRKEYWORD,_cfgInfo[_cfgCount]._cfgName))
      _snapshotCfgIndex = (int) _cfgCount;
    if (_cfgCount++ >= MAXCFGCOUNT) // wtf?
      exit(99);
  }
  return false; //NOT REACHED
}

inline const char * BrainStemSupport::getSnapshotDirIfAny() {
  if (_snapshotCfgIndex >= 0)
    return _cfgInfo[_snapshotCfgIndex]._cfgValue;
  return 0;
}

inline bool BrainStemSupport::tryLoadTags() { //< true if tags loaded okay (now or previously)
  //FLMARK();
  //fprintf(stderr,"%s\n",__PRETTY_FUNCTION__);
  bool ret = false;
  int fd = ::open(CTAGSFILE, O_RDONLY);
  if (fd < 0) return ret;
  //FLMARK();

  struct stat nowstat;
  if (fstat(fd,&nowstat) < 0) fdie("stating",CTAGSFILE);
    
  if (_tagCount == 0u || diffMTime(nowstat,_tstat)) { // if no or new tags, load
    readTags(fd);
    _tstat = nowstat;
  }

  ret = true;
  //FLMARK();
  ::close(fd);
  return ret;
}

inline void BrainStemSupport::markInputConsumed() {
  _rstat = _istat;
}

inline void BrainStemSupport::fdie(const char * op, const char * file) {
  fprintf(stderr,"Error %s %s: %s\n",
          op,
          file,
          strerror(errno));
  exit(1);
}

inline void BrainStemSupport::requestSnapshot() {
  const char * dirp = getSnapshotDirIfAny();
  if (!dirp) return;            // quick out (e.g., on the T2s)
  
  int idx = getTermIndex("CADENCE");
  unsigned cadence = 5u;
  if (idx >= 0) {
    int val = getTermValue(idx);
    if (val > 0) cadence = (unsigned) val;
  }
  time_t itime = getISecs();
  itime = (itime/cadence)*cadence; // round down to even cadence
  struct tm ltime;
  localtime_r(&itime,&ltime);
  const size_t LEN = 500u;

  char buf[LEN];
  int pos = snprintf(buf,LEN,"%s/",dirp);
  if (pos >= 0 && pos < (int) LEN) {
    strftime(&buf[pos],LEN-pos,"%Y%m%d%H%M%S.png",&ltime);
    MFM::GlobalHooks & hooks = MFM::GlobalHooks::getSingleton();
    hooks.runHook("RequestSnapshot",(void*) buf);
  }
}

inline bool BrainStemSupport::readInputFile() { // true if file exists and was read if new
  int fd;
  bool ret = false;
  fd = ::open(CINPUTFILE, O_RDONLY);
  if (fd < 0) return ret;

  struct stat nowstat;
  if (fstat(fd,&nowstat) < 0) fdie("stating",CINPUTFILE);
    
  if (diffMTime(nowstat,_istat)) {
    _istat = nowstat;
    _bufferLen = read(fd,_buffer, sizeof _buffer);
    if (_bufferLen < 0) fdie("reading",CINPUTFILE);
    _sensesRead = true;
  }
  ret = true;
  ::close(fd);
  return ret;
}

inline bool BrainStemSupport::writeOutputFile() {
  int fd;
  fd = ::open(COUTPUTFILE, O_WRONLY|O_CREAT, 0644);
  if (fd < 0) {
    fprintf(stderr,"Can't write %s: %s\n",
            COUTPUTFILE,
            strerror(errno));
    return false;
  }
  ssize_t wrote = write(fd,_buffer, _bufferLen);
  if (wrote != _bufferLen) {
    fprintf(stderr,"Incomplete write of %s %ld vs %ld\n",
            COUTPUTFILE,
            (long int) _bufferLen,
            (long int) wrote); // casts to please g++ 32bit..
    return false;
  }
  ::close(fd);
  return true;
}

inline MFM::u32 BrainStemSupport::getTermCount() const {
  return _tagCount;
}

inline int BrainStemSupport::getTermIndex(const char * name) {
  for (unsigned i = 0u; i < _tagCount; ++i) {
    if (0==strncmp(name,(const char *) &_tags[i].mTagName,MAXTAGNAMELEN))
      return (int) i;
  }
  return -1;
}

inline const char * BrainStemSupport::getTermName(unsigned index) {
  if (index >= _tagCount) return 0;
  return (const char *) &_tags[index].mTagName;
}

inline BrainStemSupport::TagType BrainStemSupport::getTermType(unsigned index) {
  if (index >= _tagCount) return TAGNONE;
  return _tags[index].mTagType;
}

inline int BrainStemSupport::getTermValue(unsigned index) {
  int pos = bufferPosOfTermIndex(index);
  if (pos < 0) return pos;

  MFM::u8 val = _buffer[pos+1];
  return (int) val; //0..255
}

inline bool BrainStemSupport::setTermValue(unsigned index,MFM::u8 val) {
  if (getTermType(index) < 2u) return false; // can't set senses and illegals
  int pos = bufferPosOfTermIndex(index);
  if (pos < 0) return false;    // can't set if we don't have access 
  _buffer[pos+1] = val;
  return true;
}

inline int BrainStemSupport::bufferPosOfTermIndex(int termidx) {
  for (unsigned i = 0u; i < (unsigned) _bufferLen; i += 2u) {
    if (_buffer[i] == (unsigned) termidx)
      return (int) i;
  }
  return -1;
}

inline unsigned char BrainStemSupport::excitation(unsigned char exc, unsigned char upv) {
  exc = (((unsigned int) exc) + upv/4 > 0xff) ? 0xff : exc + upv/4;
  return (unsigned char) exc;
}

inline unsigned char BrainStemSupport::inhibition(unsigned char inh, unsigned char upv) {
  inh = (((unsigned int) inh) + upv/4 > 0xff) ? 0xff : inh + upv/4;
  if (inh+INHIBITIONMIN >= INHIBITIONBASE) return INHIBITIONMIN;
  return (unsigned char) (INHIBITIONBASE - inh);
}

inline void BrainStemSupport::tryRouting() {
  bool routed = false;

  int bvcodeIdx = getTermIndex("BVREQ");
  if (bvcodeIdx < 0) return;
  int bvcodePos = bufferPosOfTermIndex(bvcodeIdx);
  if (bvcodePos  < 0) return;
  _bvcodeVal = _buffer[bvcodePos+1];

  {
    int bvactIdx = getTermIndex("BVACT");
    if (bvactIdx >= 0) {
      int bvactPos = bufferPosOfTermIndex(bvactIdx);
      if (bvactPos >= 0) {
        _buffer[bvactPos+1] = _bvcodeVal; // For now do as requested
      }
    }
  }

  unsigned char suplval = 0;
  {                           // Optional sensor up light
    int supl = getTermIndex("SUPL");
    if (supl >= 0) {
      int isupl = bufferPosOfTermIndex(supl);
      if (isupl >= 0) 
        suplval = _buffer[isupl+1];
    }
  }

  int mlr = getTermIndex("MLR");
  int mrr = getTermIndex("MRR");
  int slfl = getTermIndex("SLFL");
  int srfl = getTermIndex("SRFL");
  if (mlr < 0 || mrr < 0 || slfl < 0 || srfl < 0) return;

  int imlr = bufferPosOfTermIndex(mlr);
  int isrfl = bufferPosOfTermIndex(srfl);
  int imrr = bufferPosOfTermIndex(mrr);
  int islfl = bufferPosOfTermIndex(slfl);

  if (imlr < 0 || imrr < 0 || islfl < 0 || isrfl < 0) return;

  switch (_bvcodeVal) {
  case 0u: {                            // BV 2A
    _buffer[imlr+1] = excitation(_buffer[islfl+1],suplval); // slfl -> +mlr
    _buffer[imrr+1] = excitation(_buffer[isrfl+1],suplval); // srfl -> +mrr
    routed = true;
    break;
  }

  case 1u: {                            // BV 2b
    _buffer[imlr+1] = excitation(_buffer[isrfl+1],suplval); // srfl -> +mlr
    _buffer[imrr+1] = excitation(_buffer[islfl+1],suplval); // srfl -> +mrr
    routed = true;
    break;
  }

  case 2u: {                                        // BV 3a
    _buffer[imlr+1] = inhibition(_buffer[islfl+1],suplval); // slfl -> -mlr
    _buffer[imrr+1] = inhibition(_buffer[isrfl+1],suplval); // srfl -> -mrr
    routed = true;
    break;
  }

  case 3u: {                                        // BV 3b
    _buffer[imlr+1] = inhibition(_buffer[isrfl+1],suplval); // srfl -> -mlr
    _buffer[imrr+1] = inhibition(_buffer[islfl+1],suplval); // slfl -> -mrr
    routed = true;
    break;
  }

  default:
    break;
  }
  if (routed) ++_routed;
}

inline int BrainStemSupport::run() {
  while (1) {
    tryLoadTags();
    if (readInputFile())
      tryRouting();
    writeOutputFile();
    usleep(USLEEPTIME);
    if (!(_updates % 1000))
      printf("Updates %lu, %lu routed, code %u\n",
             (long unsigned int)_updates, //casts for g++ 32 bit
             (long unsigned int)_routed,
             _bvcodeVal);
    ++_updates;
  }
  return 0;
}

inline void BrainStemSupport::readTags(int fd) {
  memset(&_tags,0,sizeof(_tags));
    
  _tagCount = 0u;
  bool more = true;
  while (more) {
    char c;
    ssize_t num = read(fd,&c,1u);
    if (num!=1) break; // eof or error
    TagType tt = TAGNONE;
    if (c == '>') tt = TAGSENSE;
    else if (c == '<') tt = TAGMOTOR;
    _tags[_tagCount].mTagType = tt;
    int i = 0;
    while (i < MAXTAGNAMELEN-1) { // keep room for null byte
      num = read(fd,&c,1u);
      if (num!=1) {
        more = false;
        break;
      }
      if (c == '\n') break;
      _tags[_tagCount].mTagName[i] = c;
      ++i;
    }
    printf("Tag%d:%u,%s\n",
           _tagCount,
           _tags[_tagCount].mTagType,
           _tags[_tagCount].mTagName);
    ++_tagCount;
    if (_tagCount >= MAXTAGCOUNT) // wtf?
      exit(99);
  }
}

inline bool BrainStemSupport::readTagsFile() {
  int fd;
  bool ret = false;
  fd = ::open(CTAGSFILE, O_RDONLY);
  if (fd < 0) return ret;

  struct stat nowstat;
  if (fstat(fd,&nowstat) < 0) fdie("stating",CTAGSFILE);
    
  if (diffMTime(nowstat,_tstat)) {
    ret = true;
    _tstat = nowstat;
    readTags(fd);
  }
  ::close(fd);
  return ret;
  return false;
}


