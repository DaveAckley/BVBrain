#ifndef BRAINSTEMSUPPORT_H
#define BRAINSTEMSUPPORT_H

#include <sys/stat.h>
#include <unistd.h>

#include "UlamDefs.h"

#define MAXPATHLEN (500)
#define CBUFFERSIZE (2*256)
#define MAXTAGNAMELEN 15
#define MAXTAGCOUNT 256
#define MAXCFGCOUNT 100
#define MAXCFGNAMELEN 16
#define MAXCFGVALUELEN 200

class BrainStemSupport {
public:

  BrainStemSupport() ;

  const MFM::u32 getVersion() {
    return 0x01000007; // { 1, 0, 0, 255 etc }
  }

  bool open() ;
  bool close() ;

  bool newRead(unsigned index) { // returns true if the index was new
    if (index < _tagCount && diffMTime(_istat,_tags[index].mTagReadStat)) {
      _tags[index].mTagReadStat = _istat;
      return true;
    }
    return false;
  }

  const time_t getISecs() const { return _istat.st_mtim.tv_sec; }

  //  void setSnapshotDir(const char * dir) ;

  inline bool diffMTime(const struct stat & s1, const struct stat & s2) const {
    return
      (s1.st_mtim.tv_sec != s2.st_mtim.tv_sec) ||
      (s1.st_mtim.tv_nsec != s2.st_mtim.tv_nsec);
  }

  bool tryLoadConfig() ;

  bool tryLoadTags() ;

  inline bool newInput() const {
    return diffMTime(_istat,_rstat);
  }

  void markInputConsumed() ;

  void fdie(const char * op, const char * file) ;

  bool readInputFile() ;

  bool writeOutputFile() ;

  int getTermIndex(const char * name) ;

  MFM::u32 getTermCount() const ;

  const char * getTermName(unsigned index) ;

  enum TagType { TAGNONE, TAGSENSE, TAGMOTOR, TAGRSRV }; // TAGNONE == 0u
  TagType getTermType(unsigned index) ;

  int getTermValue(unsigned index) ; //< -1 if illegal else value (s or m)

  bool setTermValue(unsigned index, MFM::u8 val) ; //< true if index is in buffer and writable

  int bufferPosOfTermIndex(int tagidx) ;

  unsigned char excitation(unsigned char exc, unsigned char upv) ;

  unsigned char inhibition(unsigned char inh, unsigned char upv) ;

  void tryRouting() ;

  int run() ;

  bool readConfig(int fd) ;

  void readTags(int fd) ;

  bool readTagsFile() ;

  /** \returns true if the last open() (actually readInputFile())
      actually read the input file AND this is the first call to \c
      anyNewSenses() since then, otherwise \returns false.
   */
  bool anyNewSenses() {
    bool ret = _sensesRead;
    _sensesRead = false;
    return ret;
  }

  const char * getSnapshotDirIfAny() ;

  void requestSnapshot() ;

private:
  struct stat _cstat; // last stat time of config.dat load
  struct CfgInfo {
    char _cfgName[MAXCFGNAMELEN];
    char _cfgValue[MAXCFGVALUELEN];
  } _cfgInfo[MAXCFGCOUNT];
  unsigned _cfgCount;
  int _snapshotCfgIndex; // location in _cfgInfo of snapshotdir or -1

  //char _snapshotDir[MAXPATHLEN];
  unsigned char _buffer[CBUFFERSIZE];
  ssize_t _bufferLen;
  size_t _updates;
  size_t _routed;
  struct stat _istat; // last stat read by input
  struct stat _rstat; // last stat read by routing
  
  typedef struct tag {
    TagType mTagType;
    char mTagName[MAXTAGNAMELEN];
    struct stat mTagReadStat; // _istat of last user read
  } Tag;

  struct stat _tstat; // last stat time of tags load
  Tag _tags[MAXTAGCOUNT];
  unsigned _tagCount;
  unsigned char _bvcodeVal;        // current routing code

  bool _sensesRead;             //< true iff input.dat read in latest open()

  int readThrough(int fd, char delim, char * dest, unsigned max);
};

#include "BrainStemSupport.tcc"
#endif/* BRAINSTEMSUPPORT_H*/
