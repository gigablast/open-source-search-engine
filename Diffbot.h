
#ifndef DIFFBOT_H
#define DIFFBOT_H

bool printCrawlBotPage ( TcpSocket *s , 
			 HttpRequest *hr ,
			 SafeBuf *injectionResponse = NULL ) ;
//bool handleDiffbotRequest ( TcpSocket *s , HttpRequest *hr ) ;
bool sendBackDump ( TcpSocket *s,HttpRequest *hr );

#endif
