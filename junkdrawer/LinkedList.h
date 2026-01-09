// Copyright Matt Wells Nov 2002

// manages a link list of fixed-size links

class LinkedList {

 public:

	// . returns false and set g_errno on error
	// . comparison key is first 16 bytes of link data
	bool init ( int32_t linkSize , int32_t maxNumLinks );

	// . TRY to add a slot
	// . returns false if not added, true if added
	// . may kick out other links to make room
	bool addLink ( char *link );

	// get head link
	char *getHeadLink ( );

	// . get next link
	// . returns NULL if empty
	char *getNextLink ( char *link );

};
