
#include <math.h>


void            open_lexicon(char *);
void            close_lexicon(void);
void            load_index(char *);
void            load_logcharfreq(char *);
int             search_head(int, char *);
int             search_body(int, long, int, char *);
void            segment(char *,char **, long **, bool **, bool **, long **);
void            mmsegment(char *, char **, long **, bool **, bool **, long **);

unsigned int    big5_character_test(unsigned char, unsigned char);
int             big5_charactertype_test(unsigned int);
//long  tokenizeChinese(char *, char *, long *, int *, int *, long);
long tokenizeChinese ( char *, char *, long *, bool *, bool *, long *);
