#ifndef _PAGETURK_H_
#define _PAGETURK_H_

bool printCaptcha2 ( SafeBuf *sb ) ;
bool isCaptchaReplyCorrect ( TcpSocket *s ) ;
bool verifyCaptchaInput ( TcpSocket *socket ,
			  HttpRequest *r ,
			  void *st ,
			  void (callback)(void *state,TcpSocket *s) ) ;

bool isSuperTurk ( char *turkUser ) ;
bool isTurkBanned ( int64_t *tuid64 , int32_t turkIp );

extern class HashTableX g_templateTable;

#endif
