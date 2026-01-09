
#include <math.h>


void            open_lexicon(char *);
void            close_lexicon(void);
void            load_index(char *);
void            load_logcharfreq(char *);
int             search_head(int, char *);
int             search_body(int, int32_t, int, char *);
void            segment(char *,char **, int32_t **, bool **, bool **, int32_t **);
void            mmsegment(char *, char **, int32_t **, bool **, bool **, int32_t **);

unsigned int    big5_character_test(unsigned char, unsigned char);
int             big5_charactertype_test(unsigned int);
//int32_t  tokenizeChinese(char *, char *, int32_t *, int *, int *, int32_t);
int32_t tokenizeChinese ( char *, char *, int32_t *, bool *, bool *, int32_t *);
