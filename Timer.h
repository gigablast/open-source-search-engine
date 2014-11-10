
#ifndef _TIMER_H_
#define _TIMER_H_

class Timer { 
public:
        Timer () : m_start(0), m_end(0) {};
	Timer ( int64_t startTime ) : m_start(startTime), m_end(0) {};
	virtual ~Timer() {};

	virtual void start () { m_start = gettimeofdayInMillisecondsLocal(); };
	virtual void stop  () { m_end   = gettimeofdayInMillisecondsLocal(); };

	int64_t getSpan () { return m_end-m_start; };

	int64_t m_start;
	int64_t m_end;
};

class AutoTimer : public Timer {
public:
	AutoTimer  ( char *subtype, char *name, char *klass = NULL ) 
		: Timer(), m_subtype(subtype), m_name(name), m_class(klass) { 
		start(); };
	~AutoTimer () {
		stop();
		if ( m_class ) 
			log( LOG_TIMING, "%s: Took %lld ms for %s::%s", 
			     m_subtype, getSpan(), m_class, m_name );
		else
			log( LOG_TIMING, "%s: Took %lld ms for %s", 
			     m_subtype, getSpan(), m_name );
	};

	char *m_subtype;
	char *m_name;
	char *m_class;
};

class MicroTimer {
public:
	MicroTimer () : m_start(0), m_end(0) {};
	virtual ~MicroTimer () {};

	virtual void start () { m_start = gettimeofdayInMicroseconds(); };
	virtual void stop  () { m_end   = gettimeofdayInMicroseconds(); };

	uint64_t getSpan () { return m_end-m_start; };

	uint64_t m_start;
	uint64_t m_end;
};


class AutoMicroTimer : public MicroTimer {
public:
	AutoMicroTimer  ( char *subtype, char *name, char *klass = NULL ) 
		: MicroTimer(), m_subtype(subtype), m_name(name), m_class(klass) { 
		start(); };
	~AutoMicroTimer () {
		stop();
		if ( m_class ) 
			log( LOG_TIMING, "%s: Took %"UINT64" microseconds for %s::%s",
			     m_subtype, getSpan(), m_class, m_name );
		else
			log( LOG_TIMING, "%s: Took %"UINT64" microseconds for %s", 
			     m_subtype, getSpan(), m_name );
	};

	char *m_subtype;
	char *m_name;
	char *m_class;
};

#endif // _TIMER_H_
