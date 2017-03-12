#ifndef PUNCFILTER_H
#define PUNCFILTER_H

#include <iconv.h>
#include <list>
using std::list;

class puncFilter {
 public:
  puncFilter();
  ~puncFilter();
  virtual bool filterText(const char *input, const char **filteredText);

 private:
  // setMode: process the msg format:
  // char0: mode ('0'=none, '1'=all or '2'=some)
  // char1..charN: some punctuation characters in UTF8

  void setMode(const char* msg);

  int find_punctuation(list<wchar_t*> &the_list, wchar_t* src, int w_src_len);

  iconv_t my_wchar_to_utf8_convertor;
  iconv_t my_utf8_to_wchar_convertor;
  char *my_filtered_text;

  enum mode{
    PUNC_NONE = 0,
    PUNC_ALL = 1,
    PUNC_SOME = 2
  };
  mode my_mode;
  
  wchar_t *my_punctuation;
  int my_punctuation_num;
};


extern "C" {
  void *puncfilter_create();
  void puncfilter_delete(void *handle);
  bool puncfilter_do(void *handle, const char *input, const char **filteredText);
}

#endif
