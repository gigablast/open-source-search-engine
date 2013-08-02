#ifndef _PAGETURK_H_
#define _PAGETURK_H_

bool printCaptcha2 ( SafeBuf *sb ) ;
bool isCaptchaReplyCorrect ( TcpSocket *s ) ;
bool verifyCaptchaInput ( TcpSocket *socket ,
			  HttpRequest *r ,
			  void *st ,
			  void (callback)(void *state,TcpSocket *s) ) ;

bool isSuperTurk ( char *turkUser ) ;
bool isTurkBanned ( long long *tuid64 , long turkIp );

extern class HashTableX g_templateTable;

#endif
