#ifndef _USERS_
#define _USERS_

#include "gb-include.h"
#include "RdbCache.h"
#include "Tagdb.h"
#include "Pages.h"
#include "HashTableX.h"

// cache reload freq from Conf::m_users
#define USER_DATA_READ_FREQ 3600

#define MAX_USER_SIZE 50
#define MAX_PASS_SIZE 50
#define MAX_COLLS_PER_USER 5
#define MAX_IPS_PER_USER 5
#define MAX_TAGS_PER_USER 10
#define MAX_PAGES_PER_USER 15
#define USER_SESSION_TIMEOUT 18000

// fwd and back scan limits
// for user accuracy
#define ACCURACY_FWD_RANGE 10
#define ACCURACY_BWD_RANGE 10
// min number of data points
// for calculating accuracy
#define ACCURACY_MIN_TESTS 5
// max test results to store
// in tree memory
#define MAX_TEST_RESULTS 500000


// the permission bits, each Page has them in Page::m_perm
//#define USER_PUBLIC 0x01 // globally available
//#define USER_MASTER 0x02 // master admin, can see everything
//#define USER_ADMIN  0x04 // collection admin, just for a coll.
//#define USER_SPAM   0x08 // spam assassin
//#define USER_PROXY  0x10 // the proxy server
//#define USER_CLIENT 0x20 // client

// . individual user information
// . Each user record is made of 
// . allowed_collection_names + allowed_Ips + username(50 bytes max) +
//   password ( 50bytes max ) + permissions + tags 
class User{
public:
	User();
	uint16_t    getPermissions ( ){ return m_permissions; }
	int32_t     *  getTags ( ){ return &m_tagId[0]; }
	bool        verifyIp ( int32_t ip );
	bool        verifyPassword (char *pass);
	bool        verifyColl ( int32_t collNum);
	bool        verifyTagId ( int32_t tagId );
	bool        verifyPageNum ( uint16_t pageNum );
	int32_t        firstPage ( );

	//username is max of 50 chars
	//password is maximum of 50 chars
	char        m_username[MAX_USER_SIZE]; 
	char        m_password[MAX_PASS_SIZE]; 

	// user permissions
	uint16_t    m_permissions; 
	// tagdb tags
	int32_t        m_tagId[MAX_TAGS_PER_USER];
        uint16_t    m_numTagIds;

	// collection user is allowed to access
	collnum_t   m_collNum[MAX_COLLS_PER_USER];
	collnum_t   m_numColls; // num of Collections assigned
	bool        m_allColls; // true if user can access all collections

	// allowed ips
	int32_t        m_ip[MAX_IPS_PER_USER];
	uint16_t    m_numIps; // number of allowed ips (max 255)
	// . ipmasks helps to allow part of ip match
	// . points to the locations of stars
	// . Note: right now * can only be given for a complete byte
	//   of an ip
	uint32_t    m_ipMask[MAX_IPS_PER_USER];
	bool        m_allIps;// true if allowed from all Ips

	// . pages allowed
	// . top MSB is set for the pages that are allowed
	// . if all pages are allowe then allPages is set
	uint16_t    m_pages[MAX_PAGES_PER_USER];
	uint16_t    m_numPages;
	bool        m_allPages;

	// . relogin - if set user dont have to relogin
	//   between gb shutdowns
	// . only restriction is the user session 
	// . it should be valid
	bool        m_reLogin;
	
};

// . User database
// . RdbCache of user's 
// .  Record Format
//   12 byte key + 1 User record (18 bytes) 
// . User data is read from the Conf::g_users parm
//   which is linked to the Users Textbox on security control page
class Users{

public:
	// initialize all members to 0/false
	Users ();
	//
	~Users();
	// initialize the database
	bool   init();
	bool   save();	
	// get one TurkUser rec corresponding to
	// the username from m_userDB cache
	User * getUser (char *username );
	
	// add login
	bool   loginUser ( char *username, int32_t m_ip );
	bool   logoffUser ( char *username, int32_t m_ip );
	
	// verify if user is valid
	bool verifyUser ( TcpSocket *s, HttpRequest *r );
	// return username from request
	char *getUsername ( HttpRequest *r );
	
	bool hasPermission ( HttpRequest *r,
			     int32_t        page ,
			     TcpSocket   *s = NULL );

	bool hasPermission ( char *username, int32_t page );
	
	//. check is user if logged by checking
	// the login table
	//. also checks for session timeout
	//. returns null is session timeout or user not
	//  logged in
	User * isUserLogged ( char *username, int32_t m_ip );

	// load turk test results into tree
	bool   loadTestResults ( );

	bool loadUserHashTable ( ) ;

	// . get User accuracy during a particular
	//   period 
	// . return -1 if not enough data points to compute
	//   accuracy
	int32_t   getAccuracy ( char *username, time_t timestamp);

	void   makeCacheKey( char *username, key_t *cacheKey );
	// true if database is initialized correctly
	bool   m_init;

	//private:

	// loads users.dat into m_ht
	bool   loadHashTable ( );
	
	// parse a row in users.dat file
	bool   parseRow ( char *row, int32_t rowLen, User *user );
	// . parse a datum or field from a user row and store it
	//   in the User
	// . Used by parseUserRow to parse and store a user field
	void   setDatum (char *data, int32_t colNum, User *user, bool hasStar ); 
	
	//RdbCache      m_userCache; // database of user information
	//uint32_t m_userCacheAge;
	//uint32_t m_userCacheSize;

	// each slot is a key/User pair
	HashTableX m_ht;

	//File     m_userFile; // not used right now
	//uint32_t m_oldBufSize;
	//uint32_t m_oldBufReadTime;

	bool m_needsSave;
	
	// login table 
	HashTableX m_loginTable;
};

extern Users g_users;
// . Results tree consists of the user results
// . key is the result
// . format: key.n1 = 32 bit hash of username
//           key.n0 = ((int32_t)timestamp << 8 )|(isResultCorrect & 0xff );
// . using the above key we can easily find out the 
//   user's accuracy during a particular time period
extern RdbTree g_testResultsTree;
#endif
