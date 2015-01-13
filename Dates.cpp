//-*- coding: utf-8 -*-


// stjohnscollege.edu
// - lost event because we changed the implied sections algo and no
//   longer adds the address and store hours as a single implied section
// - probably should write this one off


// ingramhillusic.com
// stop it from teltescoping to the month/year pairs in the blog roll.
// detect brothers that are month/year pairs in a list and do not telescope
// to them. set their dates as DF_ARCHIVE_DATES


//. to fix folkmads.org we should allow the 3 tods to propagate up to the
//  date above them. then to avoid mult locations for event we should telescope
//  all pieces of the date telescope kinda like at the same time until we
//  encounter an address. OR allow an hr tag to propagate up unbtil it hits
//  text, then set its section therer.





// Dates.cpp revision idea:
// - i might go so far to say that any time you have different dates in 
//   a section that you are compatible with, then things are ambiguous
//   and you should give up entirely with the telescope. 
// - we use this algo for assigning addresses i think to event dates
// - we should keep the telescope up until it hits a point of ambiguity
// - but if we can contain 2+ dates from the same section in the same
//   telescope then it is not ambiguous and that is ok....
// - how would this affect our other pages?
// - would fix http://www.ingramhillmusic.com/tour/ ?
// - would fix stoart.com ?
// - would fix christchurchcinnati.com?


// --------------------
// list of events with bad times (4) (fix these first)
// --------------------

// http://christchurchcincinnati.org/worship
// - bad implied section, should be based on h2 tag, but it is based on
//   a single <p> tag with heading bit set (METHOD_ATTRIBUTE) i think
// - gets some wrong event dates
// - 12:10 should not telescope to "the Sundays" because it has
//   "wednesdays" in its title. do we have bad implied sections?
// - misses "ten o'clock" date format

// http://milfordtheatreguilde.org/Larceny.htm
// - gets some wrong event dates
// - seems to be ignore date list: oct 8th, 9th, ...
// - easy fix

// http://www.contemporaryartscenter.org/UnMuseum/ThursdayArtPlay
// - gets some wrong event dates
// - allows a store hours to telescope to all possible combos but in this
//   case it should always telescope to the "summer" in its sentence...
//   and be required to have that.
// - just allow the plain store hours to be a subdate if compared to a
//   store hours that has a seasonal or month range...
// - easy fix

// http://www.stoart.com/
// - what happened to datelistcontainer around the dow list?
// - eliminate addresses that are picture subtitles or are in 
//   picture galleries. the address is describing the picture not the
//   event.
// - asshole's schedule is not aligned with the dows. he relies on the
//   browser rendering the two table columns just right...
// - should not be allowing those list of tod ranges to telescope to
//   any dow since the dows are in their own list. i thought i had logic
//   added recently to prevent this...
// - then if such a thing happens, that list of headers should block the
//   telescoping and we end up with just a bunch of tod ranges, and we
//   should ignore any even that is just a tod range.
// - likewise, July 2010 should not telescope to Saturday then 1:30-3:30pm.
//   it can telescope to Saturday because we allow telescoping to a list
//   of headers for the new MULTIPLE HEADER algo, but then the Saturday
//   can't telescope to a non-contained or brother list of tods
// - do not consider veritcal lists the same date types, and do not allow
//   any other dates to telescope to them or past such vertical lists, also
//   the vertical list must be side-by-side with another vertical list for
//   this algo to really work. so quite a few contstraints for something
//   that is ambiguous anyhow, even if in a side-by-side list format.
// - and address for events is wrong. 
//   If the wrong address was in a sentence, like I created this work of
//   art at 1528 Madison Rd. Cinti OH then we could at least look at the
//   structure of the sentence to deduce that it was not talking about the
//   events. But it has no sentence context. 
// - maybe if the address is in a list of other "things", don't use it...
// - if the address is in a list of brothers, and the tod of the event is
//   not a brother in that list, i would say, ignore the address. the idea
//   being that the list is independent of the tod. i think this could hurt
//   some good pages though...
// - maybe we can fix by noting that the gallery address is unused and
//   set EV_UNCLEAR_ADDRESS on the events we do find. or EV_NESTED_ADDRESSES,
//   since one address is like the header of the other...
// * HOW TO FIX???
// * HARD FIXES - maybe just leave alone


// --------------------
// end list of events with bad times
// --------------------

// --------------------
// list of events with bad locations (1) (fix these next)
// --------------------

// http://www.so-nkysdf.com/Wednesday.htm
// - i think our METHOD_DOW_PURE fixed these implied sections
// - but why aren't we getting the "Hex" title?
// - ah, our implied sections are the best, they are shifted down by 2!
// - why ddin't a A=25 tagid work out? yeah if we did avgsim it would work..


// http://all-angels.com/programs/justice/
// - each event has a school name in the tod sentence, but we are not
//   recognizing that as a place!!
// - need to identify default city/state of a website for getting the schools
// * BAD EVENTS
// * NEED TO IDENTIFY THE DEFAULT CITY/STATE of A WEBSITE 
// * SUPPORT "at the following/these locations/places:"


// --------------------
// end list of events with bad locations
// --------------------


// st-margarets.org/
// - missing the thanksgiving eve as the title
// - telescoping to a fuzzy year range 2009-2010, should make that fuzzy



// http://www.ingramhillmusic.com/tour/
// - identify lists of disjoint dates. do not allow those lists to participate
//   in the telscoping process. then unless the date you are telescoping
//   from is in that list, you must ignore the dates in that list as far
//   as telescoping to them as headers. and the dates in that list can't
//   be the base of a telescope either.
// - this might be another way to fix thewoodencow.com
// - what about stoart.com, it would prevent the one list of tods from
//   combining with the other list of dows. so we would lose most of our
//   events for stoart.com
// - this basically would int16_t-circuit our combinatorics approach???
//   i.e. "comboTable" in Dates.cpp?
// - i might go so far to say that any time you have different dates in 
//   a section that you are compatible with, then things are ambiguous
//   and you should give up entirely with the telescope. 
// - how would this affect our other pages?
// - or just keep it simple and label the dates as DF_ARCHIVE_DATE since
//   their month/year list format is very popular. then just ignore such
//   dates for telescoping.


// http://www.guysndollsllc.com/page5/page4/page4.html
// - more or less ok. most events are outlinked titles.


// http://www.lilcharlies.com/brewCalendar.asp
// - Sunday should not map to 4pm-6pm but it does because we think 4pm-6pm
//   is store hours, but how can we think that? it needs to combine with
//   a dow in order to be store hours. 
// - how did we get "Sunday [[]] 4pm - 6pm" ???
// - brbrtagdelim (double br) should be enough to keep the right dow mapping
//   to the right tod.
// - bad titles because we think the strong tag portion is part of a longer
//   sentence. so do not make sentence go across the strong or bold tag
//   or italic or underline tag UNLESS the next word is lower case, etc.
//   so treat these non-breaking tags as we treat the other breaking tags.
// - BETTER SENTENCE DETECTION (EASY)



// http://sfmusictech.com/
// - hotel kabuki
// - we now get the cocktail event again since i added custom-delimeter 
//   implied sections


// http://www.guysndollsllc.com/
// - has bad telescope: "until 2:00 a.m [[]] Tuesday through Sunday (Monday)"
//   which does not have a real start time. should telescope to 
//   "4:00 p.m. until 2:00 a.m." since it should be kitchen hours.
// * INCOMPLETE EVENT TIME
// * FIX KITCHEN HOURS
// * FIX ONGOING EVENT DATE TELESCOPES

// http://www.thepokeratlas.com/poker-room/isleta-casino/247/
// - these all seem to be in november 2009 and spidered in may 2010 so the
//   dates are old
// - implied sections need help here really
// - 2009 is not being detected as a copyright date which it should be
//   cuz in a <div id=copyright>2009 The Poker Atlas</div> tag at bottom of
//   the page.
// - BETTER COPYRIGHT DETECTION. telescope around the year's sentence until
//   we hit other text. search for "copyright" in all tags telescoped to.


// http://www.southgatehouse.com/
// - misses title "Yo La Tengo" because it thinks it is in a menu and
//   gets "Non Smoking Show" as at least the same title score...
// - how to fix?
// * BAD EVENT TITLES

// http://www.cabq.gov/library/branches.html
// - we fixed the titles with our new implied sections
// - title #12 is in same implied section as #11. why? because missing <hr>
// - #1 has a bad event title. why is it getting that google map as title?



// http://www.burlingtonantiqueshow.com/7128.html
// - if city state follows ()'s which follow street, treat it as inlined still
//   that way we can get the right address here
// - use the alt=directions link as the site venue. should update the venue
//   algo to look at that. also consider "location" or "how to get here/there"
// * DISREGARD ()'s FOR INLINED ADDRESSES
// * UPDATE VENUE ALGO
// * EASY FIX

// http://www.burlingtonantiqueshow.com/
// - no location given, but if we update the venue algo as state above we
//   can default the location to the venue.
// * NEED TWO FIXES ABOVE
// * EASY FIX

// http://www.junkmarketstyle.com/item/195/burlington-antique-show
// - seems to be ok now

// http://www.queencityshows.com/tristate/tristate.html
// - July 3 & 4 is resulting in empty times but shouldn't be!
// * FIX INTERVAL COMPUTATION
// * EASY FIX

// http://www.thewomensconnection.org/Programs/Monthly_Meetups_For_Women.htm
// - need to alias non-inlined street address to its inlined equivalent
// * FIX ADDRESS ALIAS ALGO
// * EASY FIX


// http://preciousharvest.com/feed
// - rss content is not expanded... why? need to expand CDATA tags...
// * EASY FIX

// http://www.andersonparks.com/ProgramDescriptions/YoungRembrandtsSummerCamps.html 
// - thinks event date is registration date since it is after a 
//   "register now" link.
// - do not treat date is registration hours if it is 2 or less hours like
//   1 - 2:30pm, because what box office is only open for a few hours?
// * EASY FIX



// abqcsl.org
// - the youth services tod range was telescoping to "Sunday" when we had
//   an exception inisCopmatible() to fix folkmads.org, which allowed an
//   isolated tod section to telescope its tod to a section that already had
//   a tod. but really are the youth services on sunday? that does not 
//   seem clear really...
// - 3/14/10 should telescope to the store hours, but because a brother
//   section has a tod "Oct 18, 1:15PM" it doesn't.
// - 3/14/10 is in a datelistcontainer so it can't be a header
// - it should not be included anyway because its title is outlinked
// - taking out the line in isCompatible() meant for peachpundit.com actually
//   seems to bring back the 3/14/10 telescoping to sunday hours event



// http://www.arniesonthelevee.com/ 
// - needs support for "all week" to get the store hours i think


// http://schools.publicschoolsreport.com/county/NM/Sandoval.html
// - misses santo domingo school because we do note recognize the city
//   "sn domingo pblo" which would inline the "I-25 & Hwy 301" intersection.
// - but the elementary school uses a "1" instead of an "I" for "I-25"!

// http://yellowpages.superpages.com/listings.jsp?CS=L&MCBP=true&search=Find+It&SRC=&C=bicycles&STYPE=S&L=Albuquerque+NM+&x=0&y=0
// - "2430 Washington St NE" misses latitude because it is not preceded by
//   a zero nor does it have a decimal point in it

// http://www.menuism.com/cities/us/nm/albuquerque/n/7414-south-san-pedro
// - has abq,nm BEFORE the street address
// - we only got it by luck before because the state was in the name2
//   and we were calling addProperPlaces on name1 and name2 ... and the
//   city abq was in the page title
// * WHAT TO DO? -- scan headers for abq nm??????


// http://www.collectiveautonomy.net/mediawiki/index.php?title=Albuquerque
// . misses event because it can not associtate UNM with Abq, NM
// * NEED BETTER PLACE MAPPING

//. http://www.wholefoodsmarket.com/stores/albuquerque/
//  - good titles
//  - "STORES" at end should be a menu header but is not


//. http://www.switchboard.com/albuquerque-nm/doughnuts/
//  - good titles
//  - lost phone # in description when we ignored span/font tags. because
//    it is in a div hide tag.
//  - thinks switchboard.com biz category line is a menu header now that
//    implied sections groups it with that...


//http://www.zvents.com/albuquerque-nm/events/show/88543421-the-love-song-of-j-robert-oppenheimer-by-carson-kreitzer
//  - good titles
//  - gets "Feed Readers (RSS/XML" as possible title
//  - includes quite a bit of menu cruft, hopefully will fade out
//    with SEC_MENU... check for 2nd zvents.com url... (it does! see below)
//  - we should get the actual title but we get "Other future dates...".
//    i guess we should give a bonus if matches the title tag?
//  * BONUS IF MATCHES TITLE TAG


// http://www.when.com/albuquerque-nm/venues
//  - getting the place name of the event and not the event name because
//    the unverified place name has the same title score because it is
//    not verified, and because it is to the left of the time, it is
//    preferred then.
//  * NEEDS MORE PAGES SPIDERED (to verify the place names)



// http://www.zvents.com/albuquerque-nm/events/show/88688960-sea-the-invalid-mariner
//  - gets "Feed Readers (RSS/XML" as possible title
//  - "Other Future Dates & Times" title...
//  * BONUS IF MATCHES TITLE TAG



// . http://texasdrums.drums.org/albuquerque.htm
//  - alternating rows in table are all headers... we ignore these for now. 
//    but do we need header identification or something to do right?
//  - STRANGE TABLE HEADERS


//. http://www.usadancenm.org/links.html
//  - seems ok, but the best titles are mostly lowercase around the times
//    and we are getting address-y titles for the most part now
// * NO CASE PENALTY IF SENTENCE INCLUDES EVENT DATE
 

//. facebook.com
//  - gets "Full" and "Compact" as part of event description, but those are
//    options for the "View: ". so we need a special menu detector that
//    realizes one item in the list will not be a link because it is a 
//    selection menu. then "View:" should be flagged as a menu header.
//  - any link with a language name like "English (US)" should be
//    marked as SEC_MENU if in its own section and is a link.
//  * NEED SELECTION MENU DETECTOR
//  * IDENTIFY LANGUAGE LINKS AS SEC_MENU

// thingstodo.msn.com
//  - best title is "Bird Walk" in a link, but we miss it. we get
//    "Upcoming Events" instead because it gets an inheadertag boost. but
//    if we spider enough pages i would think it would get a penalty from
//    being repeated on other different event pages.
//  * NEEDS MORE PAGES SPIDERED


//. http://www.collectorsguide.com/ab/abmud.html
//  - misses jonson gallery address because of no "new mexico" in title
//  - misses atomic musuem address for same reason
//  - misses "Friday of every month at 1:30pm -- call for reservations"
//    because of SEC_HAS_REGISTRATION bit. how to fix?
//  - good titles
//  - "last modified: September 24, 2007" should be marked as a last mod
//    date by Dates.cpp and excluded completely in the min/max event id algo
//  * IDENTIFY AND IGNORE LAST MODIFIED/UPDATED DATES AND SECTIONS
//  * ADD META DESCRIPTION like we do titles for places to fix jonson gallery,.


//. http://www.abqfolkfest.org/resources.shtml
//  - american sewing guild is just in strong tags so is not its own
//    sentence, so the title algo breaks down there. but they might have
//    just as easily forwent the strong tags, then, how would we get the title?
//    i would say this is mostly title-less
//  - "For questions or comments contact the webmaster" ???? dunno... SEC_DUP?
//  - getting a Last Updated date in the event descriptions too
//  - lost a title because of TSF_MIXED_TEXT 
//    "Tango Club of Albuquerque (Argentine Tango)". should we split up the
//    sentence when it ends in a parenthetical to fix that? the new title
//    is now "DANCE" which is the generic header.
//  * IDENTIFY AND IGNORE LAST MODIFIED/UPDATED DATES AND SECTIONS




//. http://www.unm.edu/~willow/homeless/services.html
//  - a bad implied section giving us menu crap for the first few events
//  - we get header cruft for every event, so we need implied sections to
//    bind the headers to the sections they head. the header are:
//    Family Health, Child Care, School Perparation, Food, Fathers, 
//    Activities. i think they were bound with the font tags which we got
//    rid of.
//  - for "Tue. - Fri. 9 am. - 11 am" title we are missing the event
//    address in the description... what's up with that?
//    101 broadyway does not have address as a title candidate... wtf? was
//    that on purpose?? no, the other events have address as title candidates
//    misses "Noon Day Ministry" as title...
//  - missed "Closed the 1st and 15th of each month;"
//  - recognize "(no Thurs)" as except thursday.
//  - treat "Fri. pm." as "Friday night"
//  - missing "801 mountain" event... why?
//  * BETTER IMPLIED SECTIONS


// http://events.mapchannels.com/Index.aspx?venue=628
//  - pretty good. has a little menu cruft, but not too bad.


// http://www.salsapower.com/cities/us/newmexico.htm
//  - IGNORE WEBMASTER BLURBS (contact webmaster, webmaster/design...)
//  - combine copyright, webmaster, advertising blurbs at the end into
//    a tail section and ignore...
//  - "interested in advertising with us..." part of tail and probably
//    would have high SV_DUP score relative to the rest of the scores.
//  - getting "Instructores" in description of Cooperage event because
//    it is an isolated header with no elements beneath it, other than
//    the other header "Santa Fe", which is a header of an implied section.
//    i mentioned this below and called it the double header bug.
//  * DOUBLE HEADER BUG

// http://www.americantowns.com/nm/albuquerque/events/abq-social-variety-dances-2009-08-22
//  - lost event because i guess we added a delimeter-based implied section to
//    split the two tod ranges into two different "hard" sections.
//  - perhaps not EVERY dance is held at abq sw dance center, so maybe it is
//    a good/safe thing that we do not get that event any more.
//  - old comments:
//  - title is good
//  - event description has some menu cruft in it:
//  - getting view by date, view by timeframe, view by category list menu 
//    headers in event description
//  - has some real estate agent headers which is not seen as a menu
//    header because it only has one link in its menu
//  - has navigation links "Add Your <a>business</a> or <a>group</a>" which
//    are not 100% in a link, but they are in a list were each item in that
//    list does have a link in it, maybe make that exception to the SEC_MENU
//    algo, that if the section does contain link text it is acceptable,
//    even if it also contains plain text. 
//  - lone link "See All Cities in New Mexico". how to fix?
//  * SUPPORT FOR SINGLE LINK HEADER IDENTIFICATION



// http://www.ceder.net/clubdb/view.php4?action=query&StateId=31
//  - titles and descriptions seem pretty good.

// http://www.newmexico.org/calendar/events/index.php?com=detail&eID=9694&year=2009&month=11
//  - titles and descriptions seem pretty good.


// http://www.meetup.com/Ballroom-Dance-in-Albuquerque/
// - has a list of languages (language menu)
// - has a trademark blurb "trademarks belong to their respective owners"
// - has a "Read more" link that goes to another page at end of event desc.
// * LANGUAGE MENU

// http://www.abqtango.org/current.html
// - has one bad title because case is bad: 
//   "Free introductory Argentine Tango dance class" and ends up getting
//   less good titles.
// - misses another good title because it has "business district" in 
//   lower case when it shouldn't really.
// - so we are missing some good titles because of our case penalty...
//   perhaps we should not do that if the sentence includes the event date???
// * NO CASE PENALTY IF SENTENCE INCLUDES EVENT DATE


// http://www.sfreporter.com/contact_us/#
// - good title "business hours" now
// - has some menu cruft
// - has a "search" section with a bunch of forms and we get the form
//   headers in the event description
// * FORM TAG HEADER DETECTION

// http://pacificmedicalcenters.org/index.php/where-we-are/first-hill/
// - good titles
// - get some doctor's names that were not labeled as SEC_MENU because
//   they were by themselves in the list. how to fix?
// * SUPPORT FOR SINGLE LINK HEADER IDENTIFICATION


// http://www.santafeplayhouse.org/onstage.php4
// - bad implied sections for TIcket Price header etc. but we still get the
//   correct dates though
// . later we should probably consider doing a larger partition first
//   then paritioning those larger sections further. like looking 
//   ahead a move in a chess game. should better partition 
//   santafeplayhouse.org methinks this way.
// - give bonus points if implied section ends on a double <br> br tag?
// - bad titles...
// - penalizing "Performance Dates:" because it has a colon, even
//   though it is a header for a list of brothers. maybe do not penalize
//   under such conditions. this would fix the "pay-what-you-wish" title too!
// - getting bad title "Pay-what-you-wish" which is actually a "price" in
//   the ticket prices table. maybe we should penalize event titles in
//   registration sections?  or treat it as "free" (h_free in Events.cpp)
//   so we think of it has another price point. or count it for "dollarCount"
//   in Events.cpp.
// * NO HAS_COLON PENALTY if is header of a list of things

// realtor.com
// . both urs have the lat/lon twice, but the first pair misses the negative
//   sign in front of the lon and therefore it throws our whole lat/lon algo
//   out of sync and we miss the next lat/lon pair which is the real deal


// new event urls to do:

// http://www.weavespindye.org/?loc=8-00-00
// - no tod so no events
// - has no addresses
// - has one iframe, we support it

// http://www.thewoodencow.com/
// - we get store hours as events, but has unrelated events in description
//   because it is talking about things going on, but with no dates, and
//   only a "read more" link for each thing.
// * REMOVE UNRELATED BLURBS FROM EVENT DESCRIPTIONS ("read more links")
// * REMOVE SINGLE LINKS ("Subscribe (RSS)" link) from desc.
// * REMOVE WEBMASTER BLURB ("Office Space theme by Press75.com") from desc.


// http://www.thewoodencow.com/2010/07/19/a-walk-on-the-wild-side/
// - similar to root url
// * REMOVE SINGLE LINKS ("Subscribe (RSS)" link) from desc.
// * REMOVE WEBMASTER BLURB ("Office Space theme by Press75.com") from desc.


// http://www.adobetheater.org/
// - seems to be ok. got two event dates.

// http://villr.com/market.htm
// . made an exception in isCompatible() so the isolated month/day dates
//   can telescope to the store hours dates section even though that section
//   has month/day dates already.
// . if later have to undo this fix, then put a fix in that since the section
//   has "every saturday" we should ignore its month/day and allow the
//   isolated monthdays below to telescope to it. obviously "every saturday"
//   is not referring to just one monthday...
// . NEED SUPPORT FOR "mid November"
// . NEEDS SUB-EVENT SUPPORT

// http://blackouttheatre.com/Blackout_Theatre/Upcoming_Productions.html
// . has "the box performance space" but could not find a default venue
//   address on the website, and could not link this space to Abq, NM
// . NEED TO IDENTIFY THE DEFAULT CITY/STATE of A WEBSITE (by inlinkers?)


// http://vortexabq.org/
// - pretty hardcore
// - calls javascript to open the real content though and we need to support
//   that: http://vortexabq.org/ProdnProcessing.php
// - has "reqa.open("GET","ProdnProcessing.php");" and we need that file
// - misses address: 2004&frac12; Central Ave. SE, Albuquerque, NM  87106
//   but might be a copyright address
// * DOWNLOAD JAVASCRIPT IN FUNCTIONS
// * SUPPORT &frac12 in addresses

// http://folkmads.org/special_events.html
// - misses little sub tod ranges because of the rule:
//   "if ( (acc1 & acc2) == acc2 ) return false" because the header date
//   itself already has a tod range so it doesn't care about our tod range.
//   how to fix?
// - i added an exception at the end of isCompatible() to allow the isolated 
//   tods to telescope to the July date, but it was causing the pubdate tod for
//   piratecatradio.com to telescope to the play time and address, so until we
//   somehow are sure the tod is not a pubdate tod we have to leave this out
// - misses location "abq square dance center" has no city/state to pair with
// - we miss o neil's pub why? we can assume new mexico since that is in
//   the title. then we need to be able to look up a place name with no
//   city and just a state...
// * IF "ABQ" is in PLACE NAME, ASSUME CITY IS ABQ for placedb lookup
// * NEED TO IDENTIFY THE DEFAULT CITY/STATE of A WEBSITE (by inlinkers?)

// http://abqfolkdance.org/
// - misses a few tod range only sub-events because they are in an 
//   SEC_TOD_EVENT section i guess, or the telescopes fail because of the acc1
//   algo... but even if in a separate hard section, we should allow the
//   tod range to telescope to saturday nights if our section is only
//   tods and tod ranges perhaps??? 
//   "dancing begins at 8:15 and ends around 10:30."
// - the TOD ranges in the second section are sub times of the
//   first section, so they should include the first section in their
//   event description. we are using his address, right??? 
// * ADD "ENGLISH" TOD RANGES
// * SUPPORT FOR SUB EVENTS
// * SUPPORT SPECIAL RANGES:  "begins around|at 8:15 and ends around|at 10:30"


// http://newmexicojazzfestival.org/
// . is getting the box office hours as events. add to registration keywords.
// * ADD MORE REGISTRATION KEYWORDS
// * SPIDERED DATE is IN JAN 2010

// www.newmexicomusic.org/directory/index.php?content=services&select=529
// . lost event because it is in the same sentence as "box office" because
//   the author forgot to put a period in there to separate them into two
//   different sentences!
// . "Call the box office for program information: 888.818.7872 or go online 
//    at www.spencertheater.com Free public tours are offered at 10 a.m. on 
//    Tuesdays and Thursdays throughout the year."
// * BETTER SENTENCE DETECTION


// http://sybarite5.org/upcoming.htm
// - got "January, October, December 2010" as a header because its datebrother
//   bit was not set because it was at the top of the brother list. false
//   date header caused us to lose some events.
// - support NYC for address like "338 West 23rd St. NYC"
// - grabbing part of an event description from something that seems like
//   it should be paired up with an implied section with the date above it:
//   "Piotr Szewczyk The Rebel..." should be paired up with
//   "January 22,23 & 24 2010- 8:00pm" or AT LEAST in its own SEC_TOD_EVENT
//   section to prevent it from being used as a description for the
//   event with the date "July 24th 2010 7:30pm"
// - event description has another brother event desc in it... why? isn't
//   the EV_TOD_EVENT working for this???
// - NYC should be recognized sa NY,NY
// * BAD EVENT DESCRIPTION
// * NEEDS MORE IMPLIED SECTIONS

// http://corralesbosquegallery.com/
// - seems to be ok. gets the store hours.

// http://web.mac.com/bdensford/Gallery_website/Events_Calendar.html
// - the above website's events...
// - seems pretty good


// http://villr.com/market.htm
// - event description sentence mess up? "Los Ranchos Growers' and [[]] ..."
// - misses some parts of the event description because of SEC_TOD_EVENT
//   section flags. but really the brother sections that caused that were
//   actually subevents of the main date, although they did include a
//   month and daynum themselves and not a sub tod range as most sub-events
//   probably do.
// * SUPPORT FOR SUB EVENTS (month/daynum based)


// http://eventful.com/lawrenceburg/venues/lawrenceburg-fairgrounds-/V0-001-000208596-1
// - has address of lawrenceburg fairgrounds but only as an intersection
// * BETTER INTERSECTION ADDRESSES

// http://rodeo.cincinnati.com/f2/events/proddisplay.aspx?d=&prodid=3461
// - address has no street number "MainStrasse Village, Main Street 
//   Covington, KY 41011"
// - placedb should index streets without their numbers but with zip codes
//   as if they were place names, like "Tom's Grill, Abq NM". but only
//   do that if we have a gps point to go with it.
// * INDEX STREET NAMES WITHOUT NUMBERS INTO PLACEDB

// http://www.scrap-ink.com/
// - all flash, can't parse it














// http://www.newmexico.org/calendar/events/index.php?com=detail&eID=9694&year=2009&month=11
// - title of "Cost:" is bad because it preceeds colon -70%
// - best title is "Beginnin Square Dance Lessons, Albuquerque"
// - "disclaimer & use" and "Contact New Mexico TOurism Dept" should be
//   part of a menu! wtf? sentence flip flop?
// - we leave out the dollar sign '$' in one of the description sections for
//   the cost of the event since the section starts with that!
// - "More details about this meetup" probably a high SV_DUP and since it
//   starts with "more" and is in a link, will probably be excluded as a menu
//   link
// - sentence flip flop, "Promote!" should be SEC_MENU!
// - "Asst." should be in Abbreviations.h list so that "Asst. Organizers:"
//   will be just one section, and will have tiny title and desc. score since
//   prceeds a colon.
// - "Trademarks belong to their..." will have high SV_DUP count and therefore
//   minimal title and desc. score.
// - language names in a list should have minimal title and desc score.
//   but probably no need to detect since SV_DUP will be high eventually.
// * for title score ties prefer one close to the event date with highest
//   m_a
// - i would exclude really high SV_DUP dup scores from the title/desc and
//   index to keep things clear. but we do want to have field names like
//   "Category" that label other non dup-ish content. so labels are ok, but
//   not stuff like "More details about this Meetup..." which has a high
//   SV_DUP count and is not a field name for anything.

// http://www.sfreporter.com/contact_us/
// - single store hours "event"
// - probably ok but sentence flip flip bug letting in menus?


// http://www.publicbroadcasting.net/kunm/events.eventsmain
// - lost the guild cinema address, but i do not see nm or "new mexico"
//   anywhere on the page, so even though albuquerque is right after
//   "the guild cinema", if we have no state name, we can't make it work...
// - SUPPORT CITIES WITH NO STATE NAMES SOMEHOW


//mdw left off here do. pacific medical... but fix other bugs first...


// http://www.publicbroadcasting.net/kunm/events.eventsmain?action=showCategoryListing&newSearch=true&categorySearch=4025
//              getting bad titles of "Date:" 
//              need TSF_DATE_SECTION to penalize title score! so when a
//              section is a date only, do like x .05
//            - need a .90 after colon penalty TSF_AFTER_COLON...


// reverbnation.com:
//              this is a toughy!!! we got a lower case title. we have
//              mutliple bands which is ok, but we are getting categories
//              like "Latin" and "Bogota, CO" as a title. maybe discount
//              place names ...
//            - for every repeated section tag hash, compute a global
//              average title score, and apply that to boost titles that
//              might be lower case like "kimo" is on this page. i.e. we
//              are voting on the best title sections. and we should also
//              use sectiondb for this as well as this local algo.
//            - in the case of multiple events
//            - if section has a prev or next brother with the same taghash
//              then probably give a "list" TLF_IN_LIST penalty for that
//              of like maybe .80, not too harsh...


// .. consider comparing content of sections where not any dup/nondup voting
//    info, compare to sections on other websites that do have adequate voting
//    info, and if similar, maybe use that voting info. might help us nuke
//    certain types of footers and headers... legal discalimers, etc. brain
//    kinda works like this.




// ** in title tag, allow " - " to split a sentence section
// ** prefer the title that matches a section in the title tag then.



/*

BUT what about burtstikilounge??? all events are lists of links. i guess
then we just need to rely on SEC_NOT_DUP????
well kinda, the whole calendar would have SEC_NOT_DUP, but an individual
cell of the table could have SEC_DUP and/or SEC_NOT_DUP!!

to fix burts: take the list of links that we think is SEC_CRUFT_COMMON 
then look that up as a whole section and if SEC_NOT_DUP is set then do
not set SEC_CRUFT on it otherwise set it !!!
does that work?

apply to renegade links as well?
*/


//
// missed events:
//

// http://www.zvents.com/albuquerque-nm/events/show/88688960-sea-the-invalid-mariner
//              two of the events now have non-outlinked titles. good. but
//              the second date's title is wrong.
//              SEA & the Invalid Mariner...
//              * EV_OUTLINKED_TITLE casualty
//              * BAD TITLE ("Date", ignore <th> tags, SEC_CRUFT_DETECT bit)

// collectorsguide.com:
//              special subeevnt at jonson gallery starts at 5:30 but in
//              the next sentence, which actually applies to unm art gallery,
//              store hours are given up until 4pm, so this cancels out the
//              5:30pm and results in empty times. we could check to see if
//              the header is compatible before we add it??? 
//              - use title expansion algo. should be ok since address will
//              be included and we should not set EV_OUTLINKED_TITLE.
//              * BAD DATE HEADER ALGO
//              * BAD TITLES (need full expansion algo)


// abqfolfkest.org
//              need to do to-brother title section expansion algo.
//              * BAD TITLES (need full expansion algo)

// http://www.guildcinema.com/
//              one bad title. 
//              when scanning to set the title in Events.cpp we start at
//              the first date in the telscope, however we should in this
//              case start at the 6pm to get the right title. maybe pick
//              the date with the highest word # to start at, unless it does
//              not have the smallest headerCount (i.e. unless it is used
//              in more telescopes as headers than another date)
//              - set Date::m_headerCount in Dates.cpp at the end of the algo
//                just loop through the dates and set that count for all
//                Dates in a telescope not the first ptr.
//              - so pick the date in the telescope with the highest m_a
//                unless its m_headerCount is not at the min.
//              - or would event deduping fix this?
//              * BAD TITLES (start scan @ highest m_a,min m_headerCount)

// http://events.mapchannels.com/Index.aspx?venue=628
//               using "Buy Tickets from $xx" as titles. i guess we need to
//               maybe look at the table column header for "Title"?
//              * BAD TITLES (add "Buy Tickets*" links to renegade 
//                            SEC_CRUFT list)

// http://www.salsapower.com/cities/us/newmexico.htm
//              one title is "$5.00" withouth the $. maybe stop that.
//              skip titles that are just a price.
//              allow dates in titles if in same sentence as would be title.
//              that should change "with Darrin..." title to 
//              "Tuesdays with Darrin".
//              "Class at" will change to "Class at 7 p.m." but it really
//              should be "...The salsa Dance Class at 7 p.m." but i guess
//              the br tag is breaking the sentence?? we probably need to
//              really improve our sentence detector to fix that right.
//              Cooperage event is getting Instructores header as part of
//              event description because of their double heading sections.
//              FIX by not taking descriptions from brother sections that are 
//              isolated like that, when you contain its true brother in your
//              implied section. it is like a bodyless header brother. do not 
//              get descriptions from those, maybe unless it is directly above
//              you, since it could be a double header, which is rare, but 
//              that is what it is in this case.
//              * BAD TITLES (full expansion algo?)
//              * DOUBLE HEADING causing bad heading in event description

// http://www.newmexico.org/calendar/events/index.php?com=detail&eID=9694&year=2009&month=11
//              has just one event.
//              title we get is "Cost" and is below the date. we really need
//              to keep telescoping until we get text above at least one of
//              the dates in the telescope... so if we discover we have a bad
//              title then telescope until we got text on top of the lowest
//              date. try to first get the title before the date. if we 
//              telescope up until we get text before the date, if all the
//              new section we get before the date is just a title section
//              looking thing (ignoring the SEC_CRUFT) then maybe that is
//              the best title.
//              * BAD TITLES (telescope until text above the date???)

// http://www.patpendergrass.com/albnews.html
//              "saturday morning from 10:00 am - noon" is not telescoping
//              to "March 19, 2005" like it should... wtf?
//              * BAD DATE TELESCOPING

// http://www.abqtango.org/current.html
//              one title is "New" so we should ignore that probably.
//              * BAD TITLES (need full expansion algo probably for others)

// http://pacificmedicalcenters.org/index.php/where-we-are/first-hill/
//              gets a couple titles wrong. full expansion would fix it.
//              * BAD TITLES (need full expansion algo probably for others)


// http://www.santafeplayhouse.org/onstage.php4
//              we do not realize that all these dates are talking about
//              one event really... so titles are not the best...
//              also do not parse an except/closed date correctly...
//              * BAD TITLES (???)

// http://www.publicbroadcasting.net/kunm/events.eventsmain?action=showCategoryListing&newSearch=true&categorySearch=4025
//              getting bad titles of "Date:" can be fixed with full exp algo.
//              * BAD TITLES (need full expansion algo)

// http://www.dailylobo.com/calendar/
//              bad title. only one event so can't maybe do full exp algo.
//              Title is "Offered".
//              * BAD TITLE (???)

// http://www.burtstikilounge.com/burts/
//               there really are no titles.
//               so we would just take the first item in a calendar day and
//               ignoring dates would find the title to be outlinked which
//               is probably a good thing.
//               however the store hours do not really have brothers so
//               maybe do not do full expansion on them???
//               do not do the full expansion if we have a calendar page like
//               this because there are often multiple events per daynum...
//              * BAD TITLES (ignore daynums,...???)


// http://upcoming.yahoo.com/event/4888173/NM/Albuquerque/Pet-Loss-Group/The-Source/
//                single event. bad title of "Event Photos" which is really
//                SEC_CRUFT but we do not know it yet.
//              * BAD TITLE (???)


// http://events.kqed.org/events/index.php?com=detail&eID=9812&year=2009&month=11
//                has dup event. but really just one event. title is
//                "Cost:" which is wrong, and the true title is above the date.
//                consider telescoping until we get text above the date.
//               * BAD TITLE (telescope until text above the date?)

// http://entertainment.signonsandiego.com/events/eve-selis/
//                single event.
//                has title "When". really we need to identify and ignore
//                the menu cruft better.
//               * BAD_TITLE (title is "When", telescope til text above)



// http://www.mrmovietimes.com/movie-theaters/Century-Rio-24.html
//              a title is bad, it is now the address of the place.
//              lost all events because their movie titles were outlinked.
//              but the movie "2012" survived because its title was bypassed
//              because D_IS_IN_DATE was set for it!
//              - try to fix with another site page to set SEC_NOT_MENU
//              * EV_OUTLINKED_TITLE casualty
//              * BAD_TITLE ("2012" [the movie])


// http://www.trumba.com/calendars/KRQE_Calendar.rss
//  - missed address "12611 Montgomery Blvd. NE, Suite A-4 in the 
//    Glenwood Shopping Center" because city is not after or before it,
//    and i guess before when we did get this address, we had contact info
//    or something in abq. now i don't see contact info or a venue addr for
//    trumba, which is right...
//  - missed "Each weekly program is offered on Sunday at 10:30am with a 
//    repeat on Wednesday at 6:00pm". was only getting them right before
//    we added the comboTable logic in Dates.cpp to get all date combos, 
//    because of a fluke. really if Sunday and Wednesday were modified
//    by "every" or were plural then they would not be allowed to telescope
//    to the daynum/month date, which is causing them to be emptytimes.
//  - the other trumba.com url i think has a similar issue for the
//    "Transitioning Professionals..." events, which have meetings every
//    Tuesday, but the "every" is not right before the Tuesday, so we miss
//    that too. better safe than sorry!



// mdw left off here





// http://boe.sandovalcountynm.gov/location.html
//             missing address:
//             "960 FORREST RD 10 JEMEZ SPRINGS, NM 87025"

// http://www.uniquevenues.com/StJohnsNM
//             missing address:
//             "Colorado Office: 225 Main St, Opal Bldg, G-1 Edwards, CO"
//             does not like the "suite" in between street and city.

// http://eventful.com/albuquerque/venues/the-filling-station-/V0-001-001121221-1
//              before was protected by SEC_NOT_MENU logic, but now we had to
//              remove that since SEC_NOT_MENU logic is not reliable.
//              * EV_OUTLINKED_TITLE casualty

// http://events.kgoradio.com/san-francisco-ca/venues/show/4834-davies-symphony-hall
//              really it is getting bad titles now and should not have
//              any events since they are all outlinked titles...
//              * EV_OUTLINKED_TITLE casualty
//              * BAD TITLES ("Hide")

// http://www.zvents.com/albuquerque-nm/venues/show/11865-kimo-theatre
//              this lost all its events except the store hours, which is
//              expected behavior now.
//              * EV_OUTLINKED_TITLE casualty

// http://www.when.com/albuquerque-nm/venues
//              all its events had outlinked titles and it lost them all. good.
//              * EV_OUTLINKED_TITLE casualty

// http://events.kgoradio.com/san-francisco-ca/events/show/88047269-san-francisco-symphony-chorus-sings-bachs-christmas-oratorio
//              two of the events now have non-outlinked titles. good. but
//              the second and third dates' titles are wrong.
//              * EV_OUTLINKED_TITLE casualty


// http://events.sfgate.com/san-francisco-ca/venues/show/6136-exploratorium
//              all its events had outlinked titles and it lost them all. good.
//              * EV_OUTLINKED_TITLE casualty

// http://events.sfgate.com/san-francisco-ca/events/show/88884664-solstice-seed-swap
//              all of its events but one were lost because of outlinked title.
//              this is good.
//              * EV_OUTLINKED_TITLE casualty


// http://www.when.com/albuquerque-nm/venues/show/1061223-guild-cinema
//              all its events had outlinked titles and it lost them all. good.
//              * EV_OUTLINKED_TITLE casualty

// http://www.reverbnation.com/venue/153991
//              all its events had outlinked titles and it lost them all. good.
//              * EV_OUTLINKED_TITLE casualty

// http://thingstodo.msn.com/albuquerque-nm/venues/show/1139187-rio-grande-community-farm
//              "Bird walk" event title was outlinked.
//              * EV_OUTLINKED_TITLE casualty

// http://events.kgoradio.com/
//              "New riders of the purple" was an outlinked title.
//              * EV_OUTLINKED_TITLE casualty

// http://blackbirdbuvette.com/
//              "Geeks Who Drink" outlinks to another website.
//              * EV_OUTLINKED_TITLE casualty
//

// http://events.kgoradio.com/san-francisco-ca/venues/show/4834-davies-symphony-hall



// http://eventful.com/albuquerque/venues/sunshine-theater-/V0-001-001214224-7
//              only miss events because they are EV_OUTLINKED_TITLE and
//              SEC_NOT_MENU is not set for them because this is the first
//              url we index from eventful.com
//              * EV_OUTLINKED_TITLE casualty

// 

// http://www.smithsonianmag.com/museumday/venues/Albuquerque_Museum_of_Art_History.html
//              the museum hours have no associated days of week so Events.cpp
//              ignores them i guess.
//              * CLOCK DETECTION
//              * RESPIDER TESTBED

// http://www.dukecityfix.com/events/shelter-space-place-belonging
//              has "808 park ave sw albuquerque" but no state. so we
//              should assume its abq NM since that is the only city with that
//              name.
//              * SUPPORT CITIES WITHOUT STATES
//              * NEW PLACEDB KEY

// http://www.abqtango.org/current.html
//              misses some events because of those "Next Dates: ..." things
//              as well as not having city/state for addresses.
//              i hack fixed the April 2010 header problem by adding
//              "Details TBA" as an unknown location, but really we should
//              fix this right with implied sections. just need to section
//              out with implied sections BASED on the section content...
//              MORE problems of the same nature. now the 111 harvard event
//              is telescoping to the "Tues Sept 29 - Sun Oct 4" event.

// . http://www.aliconferences.com/conf/social_media_govt1209/index.htm
//       it says "sign up for your choice of these events: ..."
//       so we thinkg the event times are registration times.
//            * REGISTRATION ALGO FIX

// http://www.sdcitybeat.com/cms/location/place/stone_brewing_co/147/
// http://music.myspace.com/index.cfm?fuseaction=music.showDetails&friendid=55284962&Band_Show_ID=100037466
//             misses the event date because is sets EV_COMMENT_DATE because
//             i don't have another page from that site that has the same
//             section tag hash for the event date because they changed their
//             template!
//             * NEED MORE DATA


// http://www.abqtrib.com/news/2007/may/15/horse-therapy-gives-people-disabilities-opportunit/
//              hours dates have bad telecsope:
//              "Monday 5 to 6 p.m., 6:15 to 7:15 p.m [[]] Jan. 17, 2008"
//              where jan. 17, 2008 is a date in a link to an article, pets
//              of the week: jan 17, 2008!!
//              * do not telescope to dates that are basically clock dates
//                and SEC_FUTURE_DATE is not set...???
//               

// http://www.graffiti.org/index/history2008.html
// - gets "Cody Hudson" in event desc for gallery hours because the 
//   todSec algo in Events.cpp distributes it to all events beneath it.
// - gets the "Tues - Sun..." store hours because it is in its own todevent
//   section so EV_BAD_STORE_HOURS does not get set. and the other gallery
//   store hours i guess do not have well defined addresses.
// - "Gallery hours are Wednesday through Saturday 11:00 AM - 6pm"
//    doesn't telescope to "now - November 29, 2008" because we don't
//    understand "now". so we get some store hours from 2008.
// - streets like "60 Avant-Garde Urban Contemporary Female Artists"
// - streets like "3 Espacios"
// - streets like "4 Barack"
//              we need partition detection here. seems like all events are
//              unclear on which addresses belong to them!
//  - misses "opens on Dec 20th and runs through Feb 7th". needs to
//           support "runs" so we can make that date a single range.


// http://santafe.org/perl/page.cgi?p=maps;gid=2415
//  - miss "valentine's day WEEKEND"
//  - we miss 704 camino lejo, because it does not end in a street indicator.
//    so we think it is just a regular name of something.
//  - gets wrong dates too, because one event is between dates of another one.
//    and we do not see Meem Library as a place name for some reason.
//  * SUPPORT "<HOLIDAYNAME> WEEKEND"
//  * GET "704 Camino Lejo" by using tigerdb or recognizing "Camino" like
//    how we do with "Paseo"


// http://www.santafebotanicalgarden.org/mainpages/R_Resources.html
//              lost address from losing contact info. many just street names
//              with "Santa Fe" in the section header. maybe we should allow
//              section headers to be used in addProperPlaces() but then we'd
//              also need to assume the state is "New Mexico"! but in this
//              case we also have the place name present, so we could safely
//              try any city/state combo since we have place name! if we have
//              the place name and the street address, we should look that up
//              in placedb as another key!!! might fix christinesaari.com too
//            * NEW PLACEDB KEY

//
// santafeplayhouse
//             has "Santa Fer Playhouse is located at 142 east de vargas 
//             street..." and no city/state!
//            * NEW PLACEDB KEY


// http://www.christinesaari.com/html/news.php?psi=37 :
//            - does not get "The Kosmos" address because we need to allow
//              it to use "downtown Albuquerque" and "New HAVEN"
//            - we need to allow the whole doc to be scanned for states!!!
//            * SCAN WHOLE DOC FOR STATES


// http://www.usadancenm.org/links.html :
//              without contact info page we miss address:
//              "111 Maple Street SE (at Central), ABQ" i guess because of
//              no state.
//              might be ok if we could lookup name and street as placedb key.
//            * NEW PLACEDB KEY

// http://www.parkingcarma.com/parking_lots/401-MAIN-ST_San-Francisco/26ecdbcc-b80b-dc11-bcd7-0013723eb578/
//              lost address from losing contact info. 
//              might be ok if we could lookup name and street as placedb key.
//            * NEW PLACEDB KEY

// http://www.lasg.org/waste/richardson-letter.htm
//              bad address formation:
//              "1807 Second St #31\nSF 87505" (SF = Santa Fe)
//              might be ok if we could lookup name and street as placedb key.
//            * NEW PLACEDB KEY

// address/event loss because of no contact info:
// http://www.xeriscapenm.com/xeriscape_gardens.php
// http://www.abqtrib.com/news/2007/may/15/horse-therapy-gives-people-disabilities-opportunit/
// http://www.collectorsguide.com/ab/abmud.html (jonson gallery no city/state)
// http://www.trumba.com/calendars/albuquerque-area-events-calendar.rss
//   (fellowship hall has no city/state)
// http://obits.abqjournal.com/obits/2004/04/13 (many without city/state)

// panjea.org : romy keegan can't telescope to the "store hours" date
//              because he's in a strange section and the store hours date
//              section contains all his date type (acc1/acc2 algo). but if
//              we weaken isCompatible() for store hours then unm.edu url
//              telescopes the list of store hours it has all over and that
//              messes up.
//              - now we miss out all events on panjea.org because if one
//                sibling contains its own location we assume that every 
//                sibling must. this fixes santafe.org which has some events
//                in which we do not recognize the location and ended up
//                telescoping up the the college address header which was the
//                wrong thing to do. really we need to get better at location
//                identification.
//              * BAD DATE HEADER ALGO
//              * NEED BETTER LOCATION IDENTIFICATION


// svrocks.com: "Great American Music Hall" is not made into an address because
//              we have no convenient city/state to tie it to. but 
//              Silicon Valley is in the title tag... wtf?
//              * NO CITY STATE SPECIFIED

// http://girlsintech.net/conference2010/
//              we have a place name and street address but no city/state.
//              * NEW PLACEDB KEY

// http://www.calumetphoto.com/p/events
//              events in a frame, but robots.txt disallows the frame url!

// http://socialfresh.com/tampa/
//              now we get the two addresses in frames.

// www.marinmommies.com/create-ultimate-gingerbread-house has the bay area
// discovery museum, but has no city/state to make an address from it to even
// lookup in placedb. SOLUTION: identify dominant city/state of all events
// and assume ALL the dominating city/state pairs mentioned on the site in
// order to generate addresses. 
//              * NO CITY STATE SPECIFIED

// https://www.signmeup.com/site/reg/register.aspx?fid=N42V6K7
//   - seems somewhat ok now

// http://chamberdailydose.blogspot.com/2009/11/downtown-holiday-fest-begins-tomorrow.html
//              has "downtown fort wayne" and "tomorrow evening at 6". right
//              now we do not get any events because of that.
//              * VAGUE PLACE - downtown fort wayne
//              * TOMORROW - relative to pub date

// http://nightlifegay.blogspot.com/2009/11/its-all-pink-tomorrow-with-gifts.html
//              has street intersections, and no city/state with them either.
//              it does mention "in Philadelphia" so we need to support that
//              and we can use that to turn the street intersections into
//              addresses in Philly, PA. also uses "tomorrow night" as the
//              daynum date.
//              * STREETDB - map street intersections to a city/state
//              * INTERSECTIONS
//              * TOMORROW - relative to pub date

// http://www.atlantamusicblog.com/news/2009/11/win-tickets-to-hightide-blues-with-death-on-two-wheels-and-tesla-rossa-at-smiths-olde-bar.html
//              doesn't specify a tod, just says "turkey eve".
//              * NO TOD - just says "eve"

// http://www.washingtonpost.com/wp-dyn/content/article/2009/11/20/AR2009112004036.html
//              place have streets and various city names, but no adm1s/states.
//              we can maybe make Address classes using the place name
//              the given ... how to fix????
//              * STREETDB - streets have cities but no states...
//              * STREETDB - one street has no city or state
//              * INTERSECTIONS - Church Road and Webster Street NW
//              * AREA CODES - map area codes to a city/state as well
//              * ??? - tods are relative to the pub date, but do not say today

// http://www.stltoday.com/blogzone/the-blender/the-blender/2009/11/concert-announcement-kenny-rogers-at-family-arena/
//              Has "Family Arena" as the place, but no city/state combos
//              for it to glom onto. the newspaper is st louis based and that
//              is in the contact info... BUT the arena is in St. Charles
//              Missouri!
//              * NO CITY STATE SPECIFIED


// http://www.gwair.org/Calendar.html
//              we associate an event time with the wrong place because
//              we do not recognize its location since it is a street 
//              intersection
//              * BAD SPAN TAGS -- title/tod of one event is included in
//                     span tag of the other event, and so it misses out on
//                it address, which is an intersection. "1st Sundays, 4:00pm"
//                is the event, and that date is in the event above its
//                span tag!! wtf... bad html...

// http://www.lacrossecenter.com/currentevents.aspx?vm=0&month=12&year=2009&lngCalendarID=14,19
//              i don't know how to parse this one!
//              * BEATS ME







/*
---- just if there are multiple candidates of the same date type then
 do not select any of them in the telescope algo.
 we then need a delimter based algo to pick headers that are basically
 at the same level as the stuff the dates "under" them.

 if when telescoping you encounter the same taghash for the current section
 THEN stop and store a -1 in there to put a hault to all of them.
 this assume we have virtual sections implemented. 
 no, just don't allow lists of guys to telescope past their current section
 no matter what. or maybe you can telescope up until you hit a section
 that already has your date type. so the 2010-2037 list of years would
 stay limited to the parent section. and anyone telescoping up to that
 would take the header date right above them. but for that list of years
 2010-2037 nobody would be able to telescope to them. and for
 the table that has month/year rows intermingled with event rows, all
 of the same tag hash, it would work too!
 tr - Nov 2009
 tr - 11/13
 tr - 11/16
 tr - Dec 2009
 tr - 12/5
 tr - 12/15

 BUT other dates can telescope up to their current section.

the virtual sections would fix 
http://www.dailylobo.com/calendar/ = 
http://10.5.1.203:8000/test/doc.18080536074677915848.html

hmmm what about using delimters for events then???

can we make delimter based sections??? in Sections.cpp??
yeah, then we could use it for events.
just look for repeated alternating sections. similar to compression tech?
look for section tag hashes that have the same total occNum count and
are adjacent... then couple all of them together as a virtual section.
do a linear scan down each section.
look at sections adjacent to it by going to its m_b and getting that
section ptr. skip that ptr if its a parent. get the next sibling after it
that is in the EXACT same section it is in. stop if we leave that section
however. then for each adjacent sibling count its taghash. stop when we leave
the parent section. then look at the counts. get the smallest count.
that is how many virtual sections we have. divide all taghash counts by
that min count. repeat the scan again... but
this time when we hit the divded count for all taghashes store what we
got as a virtual section ... and continue doing that to get all virtual
sections. -- todo -- might be an alignment issue... check out later
*/



// TODO:
// support every monday, or every third monday , ...

//
// TODO:
// now for a given clock hash is it possible that some pages
// use that section for a clock, and other pages do not? let's
// wait and see before we do anything about that.
//

// TODO:

// . make a whole new set of urls for pub date detection
// . grab that sample set from buzz wiki page
// . record the correct pub date for urls in the "qatest123" coll and make sure
//   we get them each time, otherwise core dump!!
// . check the date we extract with the rss feed. that is a good test too!
//   report on that accuracy in the logs and on the stats page.

// . TODO:
//   mark the time hours that are paired up with a date
//   then pair up the remaining times with the closest unpaired dates
//   http://byekoolaidmoms.blogspot.com/2006/11/counting-down.html

// . TODO:
//   look at redir url for pub dates too! pass in firstUrl and redirUrl
//   from XmlDoc.cpp

// . TODO:
//   support partially split dates. year&month in url, month&day in body:
//   http://www.semaphoria.com/james/blogger_archives/2004/01/warning-liberal-political-ramblings-to.html

// . TODO:
//   support american/european format dection:
//   http://nietsvoormij.web-log.nl/nietsvoormij/2007/02/wat_nou_saai.html

// . TODO: what to do when 25 hour respider fails to turn up any new info
//   regarding american/european format?

// . TODO:
//   consider age of page to be when the link was added to the root page.
//   since we respider roots very frequently we can determine pretty well.

// . TODO:
//   http://harpers.org/archive/2008/12/hbc-90004012

#include "Dates.h"
#include "gb-include.h"
#include "fctypes.h"
#include "Log.h"
#include "HashTableX.h"
#include "XmlDoc.h"
#include "Abbreviations.h" // isAbbr()

#define HD_NEW_YEARS_DAY  1
#define HD_MARTIN_DAY     2
#define HD_GROUNDHOG_DAY  3
#define HD_SUPERBOWL      4
#define HD_VALENTINES     5
#define HD_PRESIDENTS     6
#define HD_ASH_WEDNESDAY  7
#define HD_ST_PATRICKS    8
//#define HD_VERNAL_EQUI    9
#define HD_PALM_SUNDAY    10
#define HD_FIRST_PASSOVER 11
#define HD_APRIL_FOOLS    12
#define HD_GOOD_FRIDAY    13
#define HD_EASTER_SUNDAY  14
#define HD_EASTER_MONDAY  15
#define HD_LAST_PASSOVER  16
#define HD_PATRIOTS_DAY   17
#define HD_EARTH_DAY      18
#define HD_SECRETARY_DAY  19
#define HD_ARBOR_DAY      20
#define HD_CINCO_DE_MAYO  21
#define HD_MOTHERS_DAY    22
#define HD_PENTECOST_SUN  23
#define HD_MEMORIAL_DAY   24
#define HD_FLAG_DAY       25
#define HD_FATHERS_DAY    26
#define HD_SUMMER_SOL     27
#define HD_INDEPENDENCE   28
#define HD_LABOR_DAY      29
#define HD_YOM_KIPPUR     30
#define HD_LEIF_ERIKSON   31
#define HD_COLUMBUS_DAY   32
#define HD_MISCHIEF_NIGHT 33
#define HD_HALLOWEEN      34
#define HD_ALL_SAINTS     35
#define HD_VETERANS       36
#define HD_THANKSGIVING   37
#define HD_BLACK_FRIDAY   38
#define HD_PEARL_HARBOR   39
#define HD_ENERGY_CONS    40
#define HD_WINTER_SOL     41
#define HD_CHRISTMAS_EVE  42
#define HD_CHRISTMAS_DAY  43
#define HD_NEW_YEARS_EVE  44

// a delimeter used below -- these are certain types of holidays
//#define HD_SPECIFIC_HOLIDAY_MAX 44

#define HD_EVERY_DAY      45
#define HD_SUMMER         46
#define HD_FALL           47
#define HD_WINTER         48
#define HD_SPRING         49

#define HD_WEEKENDS       50
#define HD_WEEKDAYS       51
#define HD_HOLIDAYS       52

#define HD_MORNING        53
#define HD_AFTERNOON      54
#define HD_NIGHT          55

#define HD_MONTH_LAST_DAY  56
#define HD_MONTH_FIRST_DAY 57

#define HD_EVERY_MONTH     58

#define HD_SCHOOL_YEAR     59
#define HD_TTH             60
#define HD_MW              61
#define HD_MWF             62

static int64_t h_funeral;
static int64_t h_mortuary;
static int64_t h_visitation;
static int64_t h_memorial;
static int64_t h_services;
static int64_t h_service  ;
static int64_t h_founded;
static int64_t h_established;

static int64_t h_seniors;
static int64_t h_a;
static int64_t h_daily;
static int64_t h_sunday;
static int64_t h_monday;
static int64_t h_tuesday;
static int64_t h_wednesday;
static int64_t h_thursday;
static int64_t h_friday;
static int64_t h_saturday;
static int64_t h_mon;
static int64_t h_tues;
static int64_t h_tue;
static int64_t h_wed;
static int64_t h_wednes;
static int64_t h_thurs;
static int64_t h_thu;
static int64_t h_thr;
static int64_t h_fri;
static int64_t h_sat;

static int64_t h_details;
static int64_t h_more;

static int64_t h_to;
static int64_t h_and;
static int64_t h_or;
static int64_t h_sun;
static int64_t h_next;
static int64_t h_this;
static int64_t h_children;
static int64_t h_age;
static int64_t h_ages;
static int64_t h_kids;
static int64_t h_toddlers;
static int64_t h_youngsters;
static int64_t h_grade;
static int64_t h_grades;
static int64_t h_day;
static int64_t h_years;
static int64_t h_continuing;
static int64_t h_through;
static int64_t h_though; // misspelling
static int64_t h_thru;
static int64_t h_until;
static int64_t h_til;
static int64_t h_till;
static int64_t h_ongoing;
static int64_t h_lasting;
static int64_t h_runs; // and runs through
static int64_t h_results;
static int64_t h_nightly;
static int64_t h_lasts; // and lasts through
static int64_t h_at;
static int64_t h_on;
static int64_t h_starts;
static int64_t h_begins;
static int64_t h_between;
static int64_t h_from;
static int64_t h_before;
static int64_t h_after;
static int64_t h_ends;
static int64_t h_conclude;
static int64_t h_concludes;
static int64_t h_time;
static int64_t h_date;
static int64_t h_the;
static int64_t h_copyright;

static int64_t h_non    ;
static int64_t h_mid    ;
static int64_t h_each   ;
static int64_t h_every  ;
static int64_t h_first  ;
static int64_t h_second ;
static int64_t h_third  ;
static int64_t h_fourth ;
static int64_t h_fifth  ;
static int64_t h_1st;
static int64_t h_2nd;
static int64_t h_3rd;
static int64_t h_4th;
static int64_t h_5th;

static int64_t h_1;
static int64_t h_2;
static int64_t h_3;
static int64_t h_4;
static int64_t h_5;

static int64_t h_of     ;
static int64_t h_year   ;
static int64_t h_month  ;
static int64_t h_week   ;

static int64_t h_weeks  ;
static int64_t h_days;
static int64_t h_months;
static int64_t h_miles;
static int64_t h_mile;
static int64_t h_mi;
static int64_t h_km;
static int64_t h_kilometers;
static int64_t h_kilometer;

static int64_t h_night      ;
static int64_t h_nights     ;
static int64_t h_evening    ;
static int64_t h_evenings   ;
static int64_t h_morning    ;
static int64_t h_mornings   ;
static int64_t h_afternoon  ;
static int64_t h_afternoons ;
static int64_t h_in         ;
static int64_t h_hours      ;
static int64_t h_are        ;
static int64_t h_is         ;
static int64_t h_semester   ;
static int64_t h_box ;
static int64_t h_office ;
static int64_t h_during     ;
static int64_t h_closed     ;
static int64_t h_closure    ;
static int64_t h_closures   ;
static int64_t h_desk;
static int64_t h_reception;

static int64_t h_st ;
static int64_t h_nd ;
static int64_t h_rd ;
static int64_t h_th ;

static int64_t h_sundays;
static int64_t h_mondays;
static int64_t h_tuesdays;
static int64_t h_wednesdays;
static int64_t h_thursdays;
static int64_t h_fridays;
static int64_t h_saturdays;

static int64_t h_summers ;
static int64_t h_autumns ;
static int64_t h_winters ;

static int64_t h_noon     ;
static int64_t h_midnight ;
static int64_t h_midday   ;
static int64_t h_sunset   ;
static int64_t h_sundown  ;
static int64_t h_dusk     ;
static int64_t h_sunrise  ;
static int64_t h_dawn     ;

static int64_t h_s;

static int64_t h_last;
static int64_t h_modified;
static int64_t h_posted;
static int64_t h_updated;
static int64_t h_by;

static int64_t h_festival;

static int64_t h_register;
static int64_t h_registration;
static int64_t h_phone;
static int64_t h_please;
static int64_t h_call;
static int64_t h_us;
static int64_t h_anytime;
static int64_t h_be;
static int64_t h_will;
static int64_t h_sign;
static int64_t h_up;
static int64_t h_signup;
static int64_t h_tickets;
static int64_t h_advance;
static int64_t h_purchase;
static int64_t h_get;
static int64_t h_enroll;
static int64_t h_buy;
static int64_t h_presale ;
static int64_t h_pre ;
static int64_t h_sale ;
static int64_t h_sales ;
static int64_t h_end ;
static int64_t h_begin ;
static int64_t h_start ;

//static int64_t h_closed ;
static int64_t h_closes ;
static int64_t h_close  ;
static int64_t h_except ;
static int64_t h_open   ;
static int64_t h_opens  ;
static int64_t h_happy;
static int64_t h_kitchen;
static int64_t h_hour;
static int64_t h_m;
static int64_t h_mo;
static int64_t h_f;

static int64_t h_late;
static int64_t h_early;

static int64_t h_since ;
static int64_t h_rsvp ;
static int64_t h_checkin ;
static int64_t h_checkout ;
static int64_t h_check ;
static int64_t h_out ;
static int64_t h_deadline ;
static int64_t h_am;
static int64_t h_pm;

// . record the event times out this many days from the current date.
// . make i 12 months out 365... but need to fix min/maxpubdate first
#define DAYLIMIT (8*30)

static bool isMonth ( int64_t wid ) ;
static char getMonth ( int64_t wid ) ;

static bool printDateElement ( Date *dp , SafeBuf *sb , Words *words ,
			       Date *fullDate ) ;

Date **g_dp2 = NULL;
Dates *g_dthis = NULL;

Dates::Dates() {
	m_numPools = 0;
	m_maxDatePtrs = 0;
	m_ttValid = false;
	m_tntValid = false;
	m_bodySet = false;
	reset();
}

Dates::~Dates() {
	reset();
}

#define POOLSIZE 32000

void Dates::reset ( ) {
	// free pool mem
	for ( int32_t i = 0 ; i < m_numPools ; i++ ) {
		mfree ( m_pools[i] , POOLSIZE , "datemempool" );
		// to be safe
		m_pools[i] = NULL;
	}
	m_numPools = 0;
	m_current = NULL;
	// reset count
	m_numDatePtrs = 0;
	m_numTotalPtrs = 0;
	m_url = NULL;
	// free that mem too
	if ( m_maxDatePtrs ) {
		mfree ( &m_datePtrs[0],m_maxDatePtrs*8,"pmem");
		m_maxDatePtrs = 0;
	}
	// we have no "best" pub date right now
	m_best     = NULL;
	m_pubDate  = -1;
	// .  1 means american
	// .  2 means european
	// . -1 means unknown
	m_dateFormat = 0;
	m_niceness   = MAX_NICENESS;
	m_changed    = 0;
	//m_urlDate    = -1;
	//m_urlDateNum = -1;
	m_urlYear    = 0;
	m_urlMonth   = 0;
	m_urlDay     = 0;
	m_firstGood  = -1;
	m_lastGood   = -1;
	m_siteHash   =  0;
	m_badHtml    = false;
	m_needQuickRespider = false;
	m_phoneXorsValid = false;
	m_emailXorsValid = false;
	m_todXorsValid = false;
	m_dayXorsValid = false;
	m_priceXorsValid = false;
	m_sftValid = false;
	m_dateBitsValid = false;
	m_current = NULL;
	m_currentEnd = NULL;
	m_overflowed = false;
	m_tids = NULL;
	m_wids = NULL;
	m_shiftDay = 0;
	m_setDateHashes = false;
	m_sections = NULL;
	m_dateFormatPanic = false;
	m_calledParseDates = false;
	m_bodySet = false;
}

// returns NULL with g_errno set on error
Date *Dates::getMem ( int32_t need ) {
	// sanity check. once we overflow, forget it! you should stop!
	if ( m_overflowed ) { char *xx=NULL;*xx=0; }
	// just use multiple pools
	if ( m_current + need <= m_currentEnd ) 
		return (Date *)m_current;
	// sanity check
	if ( need > POOLSIZE ) { char *xx=NULL;*xx=0; }
	// sanity
	if ( m_numPools+1 > MAX_POOLS ) { 
		// this error means a static limit was reached so we can't
		// parse the document
		g_errno = EBUFOVERFLOW;
		m_overflowed = true;
		// this is causing us...
		//char *u = "unknown";
		//if ( m_url ) u = m_url;
		log("dates: pools overflowed");
		return NULL;
		//char *xx=NULL;*xx=0;
	}
	// make a new pool
	char *pool = (char *)mmalloc ( POOLSIZE ,"datemempool" );
	// return NULL with g_errno set on error
	if ( ! pool ) return NULL;
	// add it
	m_pools [ m_numPools++ ] = pool;
	// set it up
	m_current    = pool;
	m_currentEnd = pool + POOLSIZE;
	return (Date *)pool;
}

// returns NULL and sets g_errno on error
Date *Dates::addDate ( datetype_t dt, dateflags_t df,int32_t a, int32_t b, int32_t num){

	// make sure we got an acceptable range of word #'s
	if ( b <= a && b != 0 && a>=0 ) { char *xx=NULL;*xx=0; }
	// assume up to 100 Date::m_ptrs[]
	int32_t need = sizeof(Date) + 100 * 4;
	// point to the new mem
	Date *DD = getMem ( need );
	// problem? g_errno should be set
	if ( ! DD ) return NULL;
	// sanity check
	if ( m_numDatePtrs>=m_maxDatePtrs || m_numTotalPtrs>=m_maxDatePtrs){
		// inc by 8k each time
		int32_t newMax = m_maxDatePtrs + 8000;
		// how much to realloc to? 8k chunks.
		int32_t need = newMax * 8;
		// realloc more
		char *pmem = (char *)mmalloc(need,"pmem");
		// on error g_errno should be set (ENOMEM, etc.)
		if ( ! pmem ) return NULL;
		// pointer for parsing up mem
		char *p = pmem;
		// start here
		Date **newDatePtrs = (Date **)p;
		// skip over
		p += newMax * 4;
		// then total ptrs
		Date **newTotalPtrs = (Date **)p;
		// skip over
		p += newMax* 4;
		// copy over from old arrays
		for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// and copy
			newDatePtrs[i] = m_datePtrs[i];
			// just in case to be safe
			m_datePtrs[i] = NULL;
		}
		// same for other array
		for ( int32_t i = 0 ; i < m_numTotalPtrs ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// and copy
			newTotalPtrs[i] = m_totalPtrs[i];
			// just in case to be safe
			m_totalPtrs[i] = NULL;
		}
		// free old crap now
		mfree ( &m_datePtrs[0] , m_maxDatePtrs * 8, "pmem" );
		// update old ptrs
		m_datePtrs  = newDatePtrs;
		m_totalPtrs = newTotalPtrs;
		// update max count
		m_maxDatePtrs = newMax;
	}

	//if ( m_numDatePtrs  >= MAX_DATE_PTRS ) {char *xx=NULL;*xx=0;}
	
	// sanity check - must be from somewhere
	//if ( ! ( df & DF_FROM_BODY ) && ! ( df & DF_FROM_URL ) ) {
	//if ( df == 0 ) {char *xx=NULL;*xx=0; }
	// sanity check
	if ( dt == 0 ) { char *xx=NULL;*xx=0; }
	// this are not simple
	dateflags_t ct = 
		DT_RANGE_ANY |
		DT_LIST_ANY  | 
		DT_COMPOUND  | 
		DT_TELESCOPE;

	// this now too
	DD->m_arrayNum = m_numTotalPtrs;
	// keep this for setting m_section/m_hardSection in ::setPart2()
	m_totalPtrs [ m_numTotalPtrs++ ] = DD;

	// . add this to tree only if a simple type
	// . because "ranges" and "lists" takeover the ptr slot of the
	//   first Date ptr in their m_ptrs[] array (inline replacement)
	if ( ! ( dt & ct ) ) m_datePtrs [ m_numDatePtrs++ ] = DD;

	// inc used mem
	m_current += sizeof(Date);

	if ( dt == DT_MONTH && (num > 12 || num < 1) ) { char *xx=NULL;*xx=0; }

	if ( dt == DT_MONTH  && num == m_urlMonth )
		df |= DF_MATCHESURLMONTH;
	if ( dt == DT_DAYNUM && num == m_urlDay   )
		df |= DF_MATCHESURLDAY;
	if ( dt == DT_YEAR   && num == m_urlYear  )
		df |= DF_MATCHESURLYEAR;
	
	// assume its a regular tod until we discover that it is really
	// "[after|before|until] 11pm"
	if ( dt == DT_TOD ) df |= DF_EXACT_TOD;

	// and set it
	DD->m_type              = dt;
	DD->m_flags             = df;
	DD->m_flags5            = 0; // i guess not enough to pass in yet
	DD->m_hasType           = dt; // type accumulator
	DD->m_a                 = a;
	DD->m_b                 = b;
	DD->m_maxa              = a;
	DD->m_mina              = a;
	DD->m_numPtrs           = 0;
	DD->m_num               = num;
	DD->m_truncated         = 0;
	DD->m_used              = NULL;
	DD->m_tagHash           = 0;
	DD->m_occNum            = 0;
	DD->m_clockHash         = 0;
	DD->m_tableCell         = NULL;
	DD->m_maxTODSection     = NULL;
	DD->m_calendarSection   = NULL;
	DD->m_lastDateInCalendar= NULL;

	// used by Events.cpp only
	DD->m_usedCount         = 0;
	DD->m_mostUniqueDatePtr = NULL;
	DD->m_section           = NULL;
	DD->m_compoundSection   = NULL;
	DD->m_hardSection       = NULL;
	DD->m_subdateOf         = NULL;
	DD->m_dupOf             = NULL;
	DD->m_dateHash64        = 0LL;
	DD->m_numFlatPtrs       = 0;
	DD->m_dates             = this;
	//DD->m_sentenceId      = 0;
	//DD->m_containingSection = NULL;

	/*
	// sanity check
	if ( a >= 0 ) DD->m_section = m_sections->m_sectionPtrs[a];
	else          DD->m_section = NULL;

	// now set m_realSection
	Section *sa = m_sections->m_sectionPtrs[a];
	// telescope until we hit a "real" section
	for ( ; sa ; sa = sa->m_parent ) {
		// get parent
		//Section *pa = sp->p_parent;
		// skip section if exactly contained by parent
		//if ( sp->m_a == pa->m_a && sp->m_b == pa->m_b )
		//	continue;
		// these are not real
		if ( m_sections->isHardSection(sa) ) break;
	}
	DD->m_hardSection       = sa;
	*/

	DD->m_month             = -1;
	DD->m_dayNum            = -1;
	DD->m_year              = -1;
	DD->m_tod               = -1;
	DD->m_dow               = -1;
	//DD->m_minDow            = 8;
	//DD->m_maxDow            = 0;
	DD->m_minYear           = 2050;
	DD->m_maxYear           = 1900;
	DD->m_minTod            = 30*3600;
	DD->m_maxTod            = 0;
	DD->m_minDayNum         = 32;
	DD->m_maxDayNum         = 0;
	DD->m_timestamp         = 0;
	DD->m_suppFlags         = 0;
	DD->m_telescope         = NULL;
	DD->m_headerCount       = 0;
	DD->m_norepeatKey       = 0LL;
	DD->m_dowBits           = 0;
	DD->m_maxYearGuess      = 0;
	DD->m_dowBasedYear      = 0;
	DD->m_minStartFocus     = 0;
	DD->m_maxStartFocus     = 0;

	// set our m_year
	if ( dt == DT_YEAR   ) DD->m_year   = num;
	if ( dt == DT_MONTH  ) DD->m_month  = num;
	if ( dt == DT_DAYNUM ) DD->m_dayNum = num;
	if ( dt == DT_TOD    ) DD->m_tod    = num;
	if ( dt == DT_DOW    ) DD->m_dow    = num;

	// set min/max dow
	if ( dt == DT_DOW ) {
		//DD->m_minDow = num;
		//DD->m_maxDow = num;
		// turn on the dow bit
		if ( num >= 8 ) { char *xx=NULL;*xx=0; }
		DD->m_dowBits |= (1<<(num-1));
	}

	if ( dt == DT_EVERY_DAY ) {
		//DD->m_minDow = 1;
		//DD->m_maxDow = 7;
		DD->m_dowBits |= (1|2|4|8|16|32|64);
	}

	if ( dt == DT_SUBWEEK && num == HD_WEEKENDS )
		DD->m_dowBits |= (1|64);
	if ( dt == DT_SUBWEEK && num == HD_WEEKDAYS )
		DD->m_dowBits |= (2|4|8|16|32);


	if ( dt == DT_SUBWEEK && num == HD_TTH )
		DD->m_dowBits |= (4|16);
	if ( dt == DT_SUBWEEK && num == HD_MW )
		DD->m_dowBits |= (2|8);
	if ( dt == DT_SUBWEEK && num == HD_MWF )
		DD->m_dowBits |= (2|8|32);

	if ( dt == DT_YEAR ) {
		DD->m_minYear = num;
		DD->m_maxYear = num;
	}

	if ( dt == DT_DAYNUM ) {
		DD->m_minDayNum = num;
		DD->m_maxDayNum = num;
	}

	if ( dt == DT_TOD ) {
		DD->m_minTod = num;
		DD->m_maxTod = num;
	}

	if ( dt == DT_TIMESTAMP )
		DD->m_timestamp = num;

	// a special hack for timestamps. we always expect event dates
	// to have m_ptrs set
	//if ( dt == DT_TIMESTAMP ) {
	//	DD->m_numPtrs = 1;
	//	DD->m_ptrs[0] = DD;
	//}

	// sanity check. do not allow anyone to use 0!
	if ( num == 0 && ! ( dt & ct ) && dt != DT_TOD ) {//&&dt != DT_MOD ) { 
		char *xx=NULL;*xx=0; }

	// set DD->m_tagHash if we should
	if ( a < 0 ) return DD;

	// get section
	//Section *sp = m_sections->m_sectionPtrs[DD->m_a];
	// int16_tcut
	//DD->m_tagHash = sp->m_tagHash;

	// indicate if we are a regular holiday like thanksgiving
	//if ( DD->m_type == DT_HOLIDAY &&
	//     DD->m_num <= HD_SPECIFIC_HOLIDAY_MAX )
	//	DD->m_suppFlags |= SF_NORMAL_HOLIDAY;
	// ok!
	return DD;
}

int32_t Dates::getDateNum ( Date *di ) {
	// what date # are we?
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		if ( m_datePtrs[i] == di ) return i;
	}
	return -1;
}

//#define UNBOUNDED (-1)
//#define MAX_TOD (24*3600-1)
//static char s_numDaysInMonth[] = { 31,28,31, 30,31,30, 31,31,30, 31,30,31 };

void Date::addPtr ( Date *ptr , int32_t i , class Dates *parent ) {

	// sanity check - do not overflow
	if ( m_numPtrs >= 100 ) { char *xx=NULL;*xx=0; }
	// get his index
	if ( parent->m_datePtrs[i] != ptr ) { char *xx=NULL;*xx=0; }

	// avoid "Friday [[]] Friday"
	//if ( ptr->m_type == DT_DOW &&
	//     m_numPtrs == 1 &&
	//     m_ptrs[0]->m_type == DT_DOW &&
	//     ptr->m_minDOW == m_ptrs[0]->m_minDOW &&
	//     ptr->m_maxDOW == m_ptrs[0]->m_maxDOW )
	//	return;

	// . nuke him
	// . NOT if he is a telescope parent though!
	if ( m_type != DT_TELESCOPE ) {
		// nuke him
		parent->m_datePtrs[i] = NULL;
		// we may replace
		if ( m_numPtrs == 0 ) 
			parent->m_datePtrs[i] = this;
	}
	// preserve all original dates and create a new Date for telescoping w/
	else if ( m_numPtrs == 0 ) {
		// sanity -- shouldn't we call addDate() to realloc?
		if ( parent->m_numDatePtrs >= parent->m_maxDatePtrs ) {
			char *xx=NULL;*xx=0; }
		parent->m_datePtrs[parent->m_numDatePtrs] = this;
		parent->m_numDatePtrs++;
	}

	// sanity check
	//if ( m_numPtrs == 0 && m_type ) { char *xx=NULL;*xx=0; }
	// sanity check - must be one of these in order to add ptrs
	//if ( !(m_flags&(DF_RANGE|DF_LIST|DF_COMPOUND|DF_TELESCOPE))) {
	//	char *xx=NULL;*xx=0;}
	// sanity check - type must be consistent in lists and ranges
	//if (!(m_flags & (DF_COMPOUND|DF_TELESCOPE))&&
	//    m_numPtrs>=1&&ptr->m_type!= m_type ) { 
	//	char *xx=NULL;*xx=0; }

	// update word range to be all inclusive for now
	if ( m_numPtrs == 0 ) {
		m_a = ptr->m_a;
		m_b = ptr->m_b;
	}
	else if ( m_type != DT_TELESCOPE ) {
		if ( ptr->m_a < m_a ) m_a = ptr->m_a;
		if ( ptr->m_b > m_b ) m_b = ptr->m_b;
	}

	if ( ptr->m_a > m_maxa ) m_maxa = ptr->m_a;
	if ( ptr->m_a < m_mina ) m_mina = ptr->m_a;

	// get crazy stuff out
	//if ( m_b - m_a > 50 ) { char *xx=NULL;*xx=0; }

	// ptr hash
	if ( m_numPtrs == 0 ) m_ptrHash = (uint32_t)(PTRTYPE)ptr;
	else {
		m_ptrHash *= 439523;
		m_ptrHash ^= (uint32_t)(PTRTYPE)ptr;
		if ( m_ptrHash == 0 ) m_ptrHash = 1234567;
	}

	// integrate him into our array
	m_ptrs [ m_numPtrs++ ] = ptr;
	// inc used mem
	parent->m_current += 4;
	// he ptrs to us
	//ptr->m_dateParent = this;

	//bool inherit = true;
	//if ( ptr->m_flags & DF_CLOSE_DATE ) inherit = false;

	// . integrate flag in case compound, DF_COMPOUND
	// . only accumulate if not a closed hours. this fixes
	//   collectorsguide.com which originally had
	//   "Every Sunday before 1pm [[]] Tue-Sun 9-5" but when we added
	//   the closed hours algo it just got
	//   "Every Sunday before 1pm [[]] holidays" which stopped it
	//   from getting "Tue-Sun 9-5" in isCompatible()
	//if ( inherit ) m_hasType |= ptr->m_hasType;
	// . no, now i am keeping close dates completely separate from
	//   non-close dates as far as telescoping, etc. goes. they are not
	//   allowed to mix dates.
	m_hasType |= ptr->m_hasType;	

	// get the ptr's flags
	datetype_t psflags = ptr->m_suppFlags;
	// do not inherit the SF_NON flag
	psflags &= ~SF_NON;

	// indicate if we are a regular holiday like thanksgiving
	//if ( ptr->m_type == DT_HOLIDAY &&
	//     ptr->m_num <= HD_SPECIFIC_HOLIDAY_MAX )
	//	psflags |= SF_NORMAL_HOLIDAY;

	// inherit the "suppFlags" so our DF_BAD_RECURRING_DOW algo works!
	//if ( inherit ) m_suppFlags |= ptr->m_suppFlags;
	m_suppFlags |= psflags;//ptr->m_suppFlags;


	// get flags
	dateflags_t flags = ptr->m_flags;
	// take out the DF_STORE_HOURS
	flags &= ~DF_STORE_HOURS;
	// like store hours
	flags &= ~DF_SCHEDULECAND;
	flags &= ~DF_WEEKLY_SCHEDULE;
	// and other page
	flags &= ~DF_ONOTHERPAGE;

	// is ptr a daynum?
	if ( ptr->m_hasType & DT_DAYNUM ) flags |= DF_HAS_ISOLATED_DAYNUM;
	// if we are adding a range ptr then the DF_HAS_ISOLATED_DAYNUM
	// flag should be stopped at that point, if it even exists
	if ( ptr->m_hasType & DT_RANGE_ANY ) flags &= ~DF_HAS_ISOLATED_DAYNUM;
	if ( m_type         & DT_RANGE_ANY ) flags &= ~DF_HAS_ISOLATED_DAYNUM;
	if ( ptr->m_hasType & DT_LIST_ANY  ) flags &= ~DF_HAS_ISOLATED_DAYNUM;
	if ( m_type         & DT_LIST_ANY  ) flags &= ~DF_HAS_ISOLATED_DAYNUM;

	m_flags |= flags;//ptr->m_flags;

	// propagate the new flags bits as well
	m_flags5 |= ptr->m_flags5;

	/*
	// set m_tagHash if we should
	if ( m_a >= 0 ) { // m_type && ( m_flags & DF_FROM_BODY ) ) {
		// get section
		Section *ss = parent->m_sections->m_sectionPtrs[m_a];
		// int16_tcut
		m_tagHash = ss->m_tagHash;
		// panic - no, parent section has no taghash
		//if ( m_tagHash == 0 || m_tagHash ==-1) {char *xx=NULL;*xx=0;}
	}
	*/

	// inherit section and taghash and hardsec from first ptr that has it
	if ( ! m_section && ptr->m_section ) {
		m_section     = ptr->m_section;
		m_hardSection = ptr->m_hardSection;
		m_tagHash     = ptr->m_section->m_tagHash;
		m_turkTagHash= ptr->m_section->m_turkTagHash32;
		if ( ! m_section     ) { char *xx=NULL;*xx=0; }
		// no! i've seen a text only doc that actually has NO hard
		// sections, so let NULL imply that the hard section is the
		// root section...
		//if ( ! m_hardSection ) { char *xx=NULL;*xx=0; }
		if ( ! m_tagHash     ) { char *xx=NULL;*xx=0; }
		if ( ! m_turkTagHash     ) { char *xx=NULL;*xx=0; }
	}


	if ( ! (m_flags & DF_FROM_BODY) && m_a >= 0 ) {char *xx=NULL;*xx=0;}

	// first ptr sets DF_STORE_HOURS 
	if ( m_numPtrs == 1 && ( ptr->m_flags & DF_STORE_HOURS ) )
		m_flags |= DF_STORE_HOURS;

	// if any thereafter is off, we are off then
	if ( ! ( ptr->m_flags & DF_STORE_HOURS ) )
		m_flags &= ~DF_STORE_HOURS;


	// first ptr sets DF_ONOTHERPAGE
	if ( m_numPtrs == 1 && ( ptr->m_flags & DF_ONOTHERPAGE ) )
		m_flags |= DF_ONOTHERPAGE;

	// if both do not have DF_ONOTHERPAGE set, then clear it
	if ( ! ( ptr->m_flags & DF_ONOTHERPAGE ) )
		m_flags &= ~DF_ONOTHERPAGE;

	// nor store hours, BUT since we telescope after setting the
	// DF_STORE_HOURS bit we will have to set it again or set it
	// after telescoping
	//m_flags &= ~DF_STORE_HOURS;

	// add it back in if we are...
	//if ( m_hasType==(DT_DOW|DT_TOD|DT_RANGE_TOD|DT_TELESCOPE) )
	//	m_flags |= DF_STORE_HOURS;
	//if(m_hasType==(DT_DOW|DT_TOD|DT_RANGE_TOD|DT_RANGE_DOW|DT_TELESCOPE))
	//	m_flags |= DF_STORE_HOURS;


	// see if this guy has an ongoing indicator before him
	//bool ongoing = (m_flags & DF_ONGOING);

	//if ( ptr->m_flags & DF_HAS_YEAR )
	//	m_flags |= DF_HAS_YEAR;

	bool invalid = false;
	// collision? if disagreement set DF_INVALID
	if ( ptr->m_month>=1 && m_month!=-1 && ptr->m_month != m_month ) {
		m_month = -2; invalid = true; }
	else if ( ptr->m_month >= 1 && m_month == -1 ) 
		m_month = ptr->m_month;

	if ( ptr->m_dayNum>=1 && m_dayNum>=1 && ptr->m_dayNum != m_dayNum ) {
		m_dayNum = -2; invalid = true; }
	else if ( ptr->m_dayNum >= 1 && m_dayNum == -1 )
		m_dayNum = ptr->m_dayNum;

	// set it to -2 so it can't be reset by adding another DT_DOW ptr!
	if ( ptr->m_dow!=-1 && m_dow>=0 && ptr->m_dow != m_dow ) {
		m_dow = -2; invalid = true; }
	else if ( ptr->m_dow != -1 && m_dow == -1 )
		m_dow = ptr->m_dow;

	if ( ptr->m_year >= 1 && m_year>=1 && ptr->m_year != m_year ) {
		m_year = -1; invalid = true; }
	else if ( ptr->m_year >=1 && m_year == -1 )
		m_year = ptr->m_year;

	// if we already got dow bits, intersect for telescope pieces
	if ( m_type == DT_TELESCOPE && m_dowBits && ptr->m_dowBits ) {
		// nuke him
		m_dowBits &= ptr->m_dowBits;
	}
	// otherwise, accumulate dow bits
	else {
		m_dowBits |= ptr->m_dowBits;
	}

	// set min/max dow
	//if ( ptr->m_dow >= 0 ) {
	//	if ( ptr->m_dow < m_minDow ) m_minDow = ptr->m_dow;
	//	if ( ptr->m_dow > m_maxDow ) m_maxDow = ptr->m_dow;
	//}

	if ( ptr->m_year != -1 ) {
		if ( ptr->m_year < m_minYear ) m_minYear = ptr->m_year;
		if ( ptr->m_year > m_maxYear ) m_maxYear = ptr->m_year;
	}

	if ( ptr->m_dayNum != -1 ) {
		if ( ptr->m_dayNum < m_minDayNum ) m_minDayNum = ptr->m_dayNum;
		if ( ptr->m_dayNum > m_maxDayNum ) m_maxDayNum = ptr->m_dayNum;
	}

	//if ( ptr->m_tod >= 0 ) { // != -1 ) {
	//	if ( ptr->m_tod < m_minTod ) m_minTod = ptr->m_tod;
	//	if ( ptr->m_tod > m_maxTod ) m_maxTod = ptr->m_tod;
	//}

	if ( ptr->m_minTod < m_minTod ) m_minTod = ptr->m_minTod;
	if ( ptr->m_maxTod > m_maxTod ) m_maxTod = ptr->m_maxTod;

	if ( ptr->m_year >=1 && m_year >=1 && ptr->m_year != m_year ) {
		m_year = -2; invalid = true; }
	else if ( ptr->m_year >= 1 && m_year == -1 )
		m_year = ptr->m_year;

	if ( ptr->m_tod >= 0 && m_tod >= 0 && ptr->m_tod != m_tod ) {
		m_tod = -2; invalid= true; }
	else if ( ptr->m_tod >= 0 && m_tod == -1 )
		m_tod = ptr->m_tod;

	// inherit non-null calendar sections
	if ( ptr->m_calendarSection )
		m_calendarSection = ptr->m_calendarSection;

	// if we are a compound or telescope adding a compound ptr then 
	// inherit his junk
	//if ( ptr->m_month  ) m_month  = ptr->m_month;
	//if ( ptr->m_dayNum ) m_dayNum = ptr->m_dayNum;
	//if ( ptr->m_year   ) m_year   = ptr->m_year;
	//if ( ptr->m_tod    ) m_tod    = ptr->m_tod;

	// return if we got a range or list
	if ( m_hasType & (DT_RANGE_ANY|DT_LIST_ANY)) return;

	/*
	// collision? if disagreement set DF_INVALID
	if ( ptr->m_month && m_month && ptr->m_month != m_month )
		m_flags |= DF_INVALID;
	if ( ptr->m_dayNum && m_dayNum && ptr->m_dayNum != m_dayNum )
		m_flags |= DF_INVALID;
	if ( ptr->m_year && m_year && ptr->m_year != m_year )
		m_flags |= DF_INVALID;
	if ( ptr->m_tod && m_tod && ptr->m_tod != m_tod )
		m_flags |= DF_INVALID;
	*/

	// . set as invalid if there was a collision
	// . not now since we have strong and weak dows, and the strong dow
	//   can override the weak dow
	//if ( invalid ) 
	//	m_flags |= DF_INVALID;

	// if we are not adding mon/day/year/tod, then we are done
	datetype_t st = 
		DT_MONTH     |
		DT_DAYNUM    |
		DT_YEAR      |
		DT_TOD       |
		DT_COMPOUND  |
		DT_TELESCOPE ;
	if ( ! ( ptr->m_type & st ) ) return;

	// otherwise, set the appropriate member var
	//if ( ptr->m_type == DT_MONTH  ) m_month  = ptr->m_num;
	//if ( ptr->m_type == DT_DAYNUM ) m_dayNum = ptr->m_num;
	//if ( ptr->m_type == DT_YEAR   ) m_year   = ptr->m_num;
	//if ( ptr->m_type == DT_TOD    ) m_tod    = ptr->m_num;
	// return if we do not have at least the month/day/year
	if ( m_month  <= 0 ) return;
	if ( m_year   <= 0 ) return;
	if ( m_dayNum <= 0 ) return;

	// make a timestamp based on that stuff
	tm ts1;
	memset(&ts1, 0, sizeof(tm));
	ts1.tm_mon  = m_month - 1;
	ts1.tm_mday = m_dayNum;
	ts1.tm_year = m_year - 1900;
	// use noon as time of day (tod)
	int32_t tod = m_tod;
	if ( tod < 0 ) tod = 0;
	ts1.tm_hour = (tod / 3600);
	ts1.tm_min  = (tod % 3600) / 60;
	ts1.tm_sec  = (tod % 3600) % 60;
	// . make the time
	// . this is -1 for early years!
	m_timestamp = mktime(&ts1);
}

// . returns false and sets g_errno on erro
// . returns true on success
// . siteHash must be saved in TitleRec and used again when deleting this
//   from indexdb, so pass in the siteHash that Msg16 uses when it calls
//   TitleRec::set(...siteHash...)
bool Dates::setPart1 ( //char       *u        ,
		       //char       *redirUrl ,
		       Url        *url      ,
		       Url        *redirUrl ,
		       uint8_t     ctype    , // contenttype,like gif,jpeg,html
		       int32_t        ip       , // ip of url "u"
		       int64_t   docId    ,
		       int32_t        siteHash ,
		       Xml        *xml      ,
		       Words      *words    ,
		       Bits       *bits     ,
		       Sections   *sections ,
		       LinkInfo   *info1    ,
		       //Dates      *odp      , // old dates from old title rec
		       HashTableX *cct      , // cct replaces odp
		       XmlDoc     *nd       , // new XmlDoc (this)
		       XmlDoc     *od       , // old XmlDoc
		       char       *coll     ,
		       int32_t        niceness ) {

	//reset();
	// if empty, set to NULL
	if ( cct && cct->getNumSlotsUsed() == 0 ) cct = NULL;
	// must have been called
	//if ( ! m_calledParseDates ) { char *xx=NULL;*xx=0; }
	// save
	m_coll     = coll;
	m_url      = url;
	m_redirUrl = redirUrl;
	m_od       = od;
	// save this
	m_siteHash = siteHash;
	m_niceness = niceness;
	m_bits     = bits;
	//if ( bits ) m_bits = bits->m_bits;

	g_dp2 = m_datePtrs;

	m_contentType = ctype;

	// sanity. parseDates() should have set this when XmlDoc
	// called it explicitly before calling setPart1(). 
	// well now it no longer needs to call it explicitly since
	// xmldoc calls getAddresses() before setting the implied
	// sections. and getAddresses() calls getSimpleDates() which calls
	// this function, setPart1() which will call parseDates() below.
	//if ( m_nw != words->m_numWords ) { char *xx=NULL; *xx=0; }

	// . get the current time in utc
	// . NO! to ensure the "qatest123" collection re-injects docs exactly
	//   the same, use the spideredTime from the doc
	// . we make sure to save this in the test subdir somehow..
	//m_now      = nd->m_spideredTime; // getTimeSynced();

	m_sections = sections;
	m_words    = words;
	m_wptrs    = words->getWords();
	m_wlens    = words->getWordLens();
	m_wids     = words->m_wordIds;
	m_tids     = words->m_tagIds;
	m_nw       = words->m_numWords;
	m_docId    = docId;
	// for getting m_spideredTime
	m_nd       = nd;
	// int16_tcut
	//Sections *ss = sections;

	// parse up spidered time if we are an open-ended range from here on
	time_t ts = nd->m_spideredTime;
	m_spts = localtime ( &ts );

	// . the date specified in the rss/atom feed is the best
	// . the <pubDate> tage from the rss
	// . loop through the Inlinks
	Inlink *k = NULL;
	for ( ; info1 && (k=info1->getNextInlink(k)) ; ) {
		// breathe
		QUICKPOLL(m_niceness);
		// does it have an xml item? skip if not.
		if ( k->size_rssItem <= 1 ) continue;
		// check xml for pub date
		log(LOG_DEBUG,"date: getting pub date from rss");

		// make xml from it
		Xml itemXml;
		if ( ! k->setXmlFromRSS ( &itemXml , m_niceness ) )
			// return false on error with g_errno set
			return false;
		// . get the date tag
		// . is it rss or atom?
		int32_t  dateLen;
		// false = skip leading spaces
		char *date = itemXml.getString   ( "pubDate",&dateLen,false );
		// atom?
		if ( ! date ) date=itemXml.getString("created",&dateLen,false);
		// if nothing, go to next
		if ( ! date ) continue;
		// rdf? look for dc:date
		for ( int32_t i = 0; i < itemXml.m_numNodes-1; i++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			XmlNode *nn = &itemXml.m_nodes[i];
			// skip text nodes
			if ( nn->m_nodeId == 0 ) continue;
			// check the node for "dc:date"
			if ( nn->m_tagNameLen == 2 &&
			     nn->m_nodeLen > 7 &&
			     strncasecmp(nn->m_tagName,"dc:date", 7 ) == 0 ) {
				date    = itemXml.m_nodes[i+1].m_node;
				dateLen = itemXml.m_nodes[i+1].m_nodeLen;
				break;
			}
		}
		// if not there, skip
		if ( ! date ) continue;
		// get words
		Words ww;
		if ( ! ww.set ( date                     ,
				dateLen                  ,
				TITLEREC_CURRENT_VERSION ,
				true                     , // compute Ids?
				m_niceness               ))
			// return false with g_errno set on error
			return false;
		// determine flag
		dateflags_t defFlags = DF_FROM_RSSINLINK;
		// is it local?
		if ( k->m_ip == ip ) defFlags |= DF_FROM_RSSINLINKLOCAL;
		// . now parse up just those words
		// . returns false and sets g_errno on error
		// . set default flag to indicate from an rss inlink
		if (!parseDates(&ww,defFlags,NULL,NULL,niceness,NULL,CT_HTML)) 
			return false;
	}

	// . get date from the "datenum" meta tag
	// . this allows client's to use datedb for arbitrary numbers
	// . for search for meta date
	// . sometimes "xml" is NULL if just parsing the url
	if ( xml ) {
		int32_t metaDate = -1;
		char buf [ 32 ];
		int32_t bufLen ;
		// do they got this meta tag?
		bufLen = xml->getMetaContent ( buf,32,"datenum",7);
		// should be in seconds since the epoch
		if ( bufLen > 0 ) metaDate = atoi ( buf );
		// int16_tcut
		dateflags_t df = DF_FROM_META;
		// . add that now too
		// . this returns false and sets g_errno on error
		if ( metaDate>0 && 
		     ! addDate(DT_TIMESTAMP,df,-1,0,metaDate))
			return false;
	}

	// int16_tcut
	char *u = ""; 
	if ( m_url ) u = m_url->getUrl();

	// . returns false and sets g_errno on error
	// . sets m_dateFromUrl
	// . sets it to -1 if none
	m_urlYear  = 0;
	m_urlMonth = 0;
	m_urlDay   = 0;
	int32_t urlTimeStamp=parseDateFromUrl(u,&m_urlYear,&m_urlMonth,&m_urlDay);
	// add the url date if we had one
	if ( urlTimeStamp && m_urlDay && m_urlMonth && m_urlYear ) {
		// int16_tcut
		dateflags_t df = DF_FROM_URL ; // | DF_NOTIMEOFDAY;
		// use noon for time of day, which is 17:00 UTC
		//int32_t tod = (12 + 5) * 3600;
		// make 3 simple dates
		int32_t ni = m_numDatePtrs;
		int32_t nj = m_numDatePtrs+1;
		int32_t nk = m_numDatePtrs+2;
		Date *di = addDate (DT_DAYNUM,df,-1,-1,m_urlDay);
		if ( ! di ) return false;
		Date *dj = addDate (DT_MONTH ,df,-1,-1,m_urlMonth);
		if ( ! dj ) return false;
		Date *dk = addDate (DT_YEAR  ,df,-1,-1,m_urlYear);
		if ( ! dk ) return false;
		// make a compound date
		Date *DD = addDate ( DT_COMPOUND,0,-1,-1,0);
		if ( ! DD ) return false;
		// and add those 3 simple dates to "DD"
		DD->addPtr ( di , ni , this );
		DD->addPtr ( dj , nj , this );
		DD->addPtr ( dk , nk , this );
		//if (!addDate(dt,df,-1,0,urlDay,urlMonth,urlYear))
		//	return false;
	}


	// . now get the dates from the body of the doc
	// . returns false and sets g_errno on error
	// . make sure "url" is non-null otherwise we are probably in a set2()
	//   call and we do not want to do an infinite recurrence loop
	//if ( words && u && ! set2 ( words , m_niceness ) ) return false;
	if ( words && u && ! parseDates (words,DF_FROM_BODY,bits,sections,
					 niceness,m_url,m_contentType))
		return false;

	// . call this a final time and link dates in the same sentence
	// . linkDatesInSameSentence = true
	// . fixes santafe.org which has
	//   "The Saturday market is open from 10 a.m.-3 p.m" and we need those
	//   to be linked together in a compound.
	if ( ! makeCompounds ( words , 
			       false ,   // monthDayOnly?
			       true  ,   // linkDatesInSameSentence?
			       false ) ) // ignoreBreakingTags
		return false;

	// try without it as well...
	//if ( ! makeCompounds ( words , false , false ) ) return false;

	// if nothing, return now
	if ( m_numDatePtrs <= 0 ) return true;

	// sanity check - must be set from parseDates()
	if ( h_open == 0 ) { char *xx=NULL;*xx=0; }

	//
	// now since we no longer set Date::m_section and m_hardSection
	// in addDate() and addPtr() we have to make up for it here. we are
	// no longer allowed to use the Sections class in Dates::parseDates()
	// because Sections::set() calls parseDates() because it uses the dates
	// to set implied sections that consist of a dom/dow header and tod
	// subjects. i did hack Date::addPtr() to inherit the m_hardSection,
	// m_section and m_tagHash from the first ptr though so that the
	// telescoping code below here will set those things.
	//
	// CONSIDER moving this to setpart2 if implied sections are hard
	// sections. because the sections class does not have implied sections
	// inserted at this point.
	//
	for ( int32_t i = 0 ; i < m_numTotalPtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		//Date *di = m_datePtrs[i];
		Date *di = m_totalPtrs[i];
		// skip if none
		//if ( ! di ) continue;
		// get this
		int32_t a = di->m_a;
		// skip if in url
		if ( a < 0 ) continue;
		// get section date is in, if any
		Section *sa = m_sections->m_sectionPtrs[a];
		// sanity check
		di->m_section = sa;
		// set tag hash
		di->m_tagHash = sa->m_tagHash;
		di->m_turkTagHash = sa->m_turkTagHash32;
		// telescope until we hit a "real" section
		for ( ; sa ; sa = sa->m_parent ) {
			// get parent
			//Section *pa = sp->p_parent;
			// skip section if exactly contained by parent
			//if ( sp->m_a == pa->m_a && sp->m_b == pa->m_b )
			//	continue;
			// these are not real
			if ( m_sections->isHardSection(sa) ) break;
		}
		di->m_hardSection = sa;
	}

	//
	// . kill dates in bad sections
	// . had to move this from parseDates() since it is called
	//   from Sections::set() now and can not use the Sections class
	// . the div style display none tags are SEC_HIDDEN now
	sec_t badFlags =SEC_MARQUEE|SEC_STYLE|SEC_SCRIPT|SEC_SELECT|
		SEC_HIDDEN|SEC_NOSCRIPT;
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get section
		Section *sd = di->m_section;
		// skip if none
		if ( ! sd ) continue;
		// skip if not bad
		if ( ! ( sd->m_flags & badFlags ) ) continue;
		// kill it otherwise, like if in <option> tag
		m_datePtrs[i] = NULL;
	}
	

	//
	// moved from parseDates()
	//

	/*
	// do not allow any dates to be headers if we are javascript because
	// all dates are pretty much bogus and slow us down!
	int32_t ndp = m_numDatePtrs;
	// or if a javascript file -- do not do telescoping algo! tends to
	// really slow things down!!
	if ( m_contentType == CT_JS ) ndp = 0;


	///////////////////////////////////
	//
	// set Date::m_sentenceId, m_sentStart, m_sentEnd
	//
	///////////////////////////////////
	int32_t sentenceId = 1;
	int32_t sentStart  = -1;
	int32_t k = 0;
	// set the sentence id of each date
	for ( int32_t i = 0 ; i <= nw ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// take care of the tail
		if ( i == nw ) goto skip;
		// need a punct word
		if ( wids[i] ) {
			// set start of sentence if we should
			if ( sentStart == -1 ) sentStart = i;
			continue;
		}
		// skip tags too
		if ( tids[i] ) {
			// some tags can be in sentences without breaking them
			if ( tids[i] == TAG_B ) continue;
			if ( tids[i] == TAG_I ) continue;
			// crap, some pages use <br> to make sections.
			// unfortunately microsoft front pages uses <br>
			// to break lines even in the same sentence... but
			// http://www.salsapower.com/cities/us/newmexico.htm
			// uses it as a section breaker so we need that 
			// otherwise we miss the event on Longview dr.
			// no, header algo is much loser now so i think we
			// get the int32_tview dr. ok
			//if ( tids[i] == TAG_BR ) continue;
			// otherwise, break the sentence
			goto skip;
		}
		// must have a period with space after
		if ( wptrs[i][0]!='.'          ) continue;
		if ( ! is_wspace_a(wptrs[i][1]) ) continue;
		// . skip if part of date
		// . i.e. "5 p. m."
		if ( m_bits && (m_bits[i] & D_IS_IN_DATE) ) continue;
		// or if preceeding alnum word is an abbreviation like "dr."
		if ( i-1>=0 && isAbbr(wids[i-1]) ) continue;
		// skip down here to wrap up the loop
	skip:
		// now assign sentence id of the past dates
		for ( ; k < ndp ; k++ ) {
			// int16_tcut
			Date *dk = m_datePtrs[k];
			// skip if none
			if ( ! dk ) continue;
			// stop if breach
			if ( dk->m_a > i ) break;
			// assign it otherwise
			dk->m_sentenceId = sentenceId;
			dk->m_sentStart  = sentStart;
			dk->m_sentEnd    = i;
		}
		// increment it now for next guys
		sentenceId++;
		// set this
		sentStart = -1;
	}
	*/

	
	//////////////////////////////////
	//
	// . set DF_CLOSE_DATE
	// . determine which times are indicating when the place is CLOSED
	// . if sentence has the word "closed" in it, then all dates in
	//   that sentence are closed dates, but if the word "closed" is
	//   separating multiple dates then only the dates on the right
	//   are "closed" dates
	//
	///////////////////////////////////

	int32_t ci = 0;

	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if from url!
		if ( di->m_a < 0 ) continue;
		// get section of the date
		Section *sd = di->m_section;
		// scan up until sentence section
		for ( ; sd ; sd = sd->m_parent )
			if ( sd->m_flags & SEC_SENTENCE ) break;
		// crazy! we should have been our own sentence at least
		//if ( ! sd ) { char *xx=NULL;*xx=0; }
		// no, not if javascript. in Sections.cpp it does not
		// call addSentenceSections() if it is javascript content
		// because it is too crazy!
		// if ( m_contentType != CT_JS && ! addSentenceSections() )
		if ( ! sd ) continue;
		// get range of sentence
		int32_t a = sd->m_a;
		int32_t b = sd->m_b;
		// make sure in doc body
		if ( a < 0 ) continue;
		// is the word "open" or "opens" in there?
		int32_t open = -1;
		int32_t closed = -1;
		// assume this
		int32_t dateStart = a;
		int32_t dateEnd   = b;
		// scan setence for the word "closed" or "except"
		int32_t k; for ( k = a ; k < b ; k++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not wid
			if ( ! m_wids[k] ) continue;
			if ( m_wids[k] == h_open  )  open = k;
			if ( m_wids[k] == h_opens )  open = k;
			// stop if we hit "closed" or "except"
			if ( m_wids[k] == h_closed ) { closed = k; break; }
			if ( m_wids[k] == h_closes ) { closed = k; break; }
			if ( m_wids[k] == h_closure) { closed = k; break; }
			// only dates after "except" are "close dates"
			if ( m_wids[k] == h_except ) { 
				closed = k; open = -2; break; }
		}
		// if not found, keep going
		if ( closed == -1 ) continue;

		//
		// fix "Monday - Friday .. (closed Sunday)" for burtstikilounge
		// because it thinks we are closed Monday-Friday otherwise.
		// because now the parenthetical is part of the sentence 
		// because we ignore the br tag as a sentence breaker because
		// "closed" is lower case. see addSentences() function in
		// Sections.cpp for more info.
		//

		// scan the sentence to see if we are in parentheses
		for ( int32_t x = a ; x < k ; x++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not punct
			if ( m_wids[x] ) continue;
			// skip tag
			if ( m_tids[x] ) continue;
			// if contains a parenthes mark it for scan start
			if ( ! m_words->hasChar(x,'(') ) continue;
			// mark it and see if we can get even closer!
			dateStart = x;
		}
		// stop prematurely at any ending parens
		for ( int32_t y = k + 1 ; y < b ; y++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not punct
			if ( m_wids[y] ) continue;
			// skip tag
			if ( m_tids[y] ) continue;
			// if contains a parenthes mark it for scan start
			if ( ! m_words->hasChar(y,'(') ) continue;
			// got one, stop
			dateEnd = y;
			break;
		}
			
		// otherwise, scan dates
		for ( ; ci < m_numDatePtrs ; ci++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// int16_tcut
			Date *dc = m_datePtrs[ci];
			// skip if none
			if ( ! dc ) continue;
			// skip if before close and after an open
			if ( open != -1 && dc->m_a < k ) continue;
			// skip if before sentence or before parens
			if ( dc->m_a < dateStart ) continue;
			// stop if past the sentence
			if ( dc->m_a >= dateEnd ) break;
			// skip if before the close/closed word and has
			// a tod to fix 
			// "8 am-12 noon, 1pm-3:30pm [[]] 1st and 15th" for
			// unm.edu, those tod ranges are not close dates.
			if ( dc->m_a < closed && (dc->m_hasType & DT_TOD) ) 
				continue;
			// it is after the "closed"
			// temporarily disable this for debugging intersections
			dc->m_flags |= DF_CLOSE_DATE;
			// unset fuzzy then, if it was set
			dc->m_flags &= ~DF_FUZZY;
		}
		// if no more dates to scan, stop
		if ( ci >= m_numDatePtrs ) break;
	}		

	// the same algorithm but for table row/col headers! might
	// have "closed" in the column header in the table
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get table cell
		Section *cell = di->m_tableCell;
		// skip if not in a table
		if ( ! cell ) continue;
		// set this flag if table header has "close" etc. in it
		if ( isCloseHeader ( cell->m_headColSection ) ||
		     isCloseHeader ( cell->m_headRowSection ) ) {
			di->m_flags |= DF_CLOSE_DATE;
			// unset fuzzy then, if it was set
			di->m_flags &= ~DF_FUZZY;
		}
	}

	//
	// for each date ptr, find the next date ptr of the
	// same tag hash. then find the first parent section in common between
	// those two dates and do not allow telescoping of EITHER date
	// BEYOND that section. 
	//
	// fixes: <tr><td class=month>Nov 2009</td><td class=day>Sun</td>...
	// so all the days of week do not telescope outside the calendar.
	//
	// breaks: <parent>
	//         <tag1>nov 3 2009 and also on dec 5 2009</tag1>
	//         <tag2>7pm,8pm and always of course at 9pm on Sundays</tag2>
	//         <tag3>8pm</tag3>
	//         </parent>
	// because these guys are seen as lists and not allowed to telescope.
	// BUT they should be allowed within <parent>

	// need a way to detect the date containers better.
	// this algo still allows cross section pairing up like these two:
	// <item><blah>dec 5, 7:30pm<blah></item1>
	// <item><blah>dec 4-5 7-pm</blah></item2>

	// will allow them to pair up with fellow list members!

	// maybe don't count if dj is in exact same section.
	// if ( dj->m_section == si ) continue; ???

	// we could identify containers


	// 1. do another container limiting algo where it is based on 
	//    find dj's matching type?

	//int64_t h_noon     = hash64b("noon");
	//int64_t h_midnight = hash64b("midnight");


	//
	// DF_FUZZY algo was here
	//

	//
	// moved DF_HAS_STRONG_DOW setting algo from here into parseDates() so
	// we can use it for Sections algo
	//

	/////////////////////////////
	//
	// set DF_NONEVENT_DATE
	// 
	// . this means it is a date when tickets go on sale date, etc.
	// . phone hours
	// . deadline dates
	//
	/////////////////////////////
	// do not do this logic if we are javascript because we do not set
	// SEC_SENTENCE if the file is javascript
	int32_t imax = m_numDatePtrs;
	if ( m_contentType == CT_JS ) imax = 0;
	//
	// if it is a place to buy tickets or register for an event then
	// let's set this flag so Events.cpp can ignore it!
	for ( int32_t i = 0 ; i < imax ; i++ ) {
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if not in body (i.e. from url)
		if ( ! (di->m_flags & DF_FROM_BODY) ) continue;
		// how did this happen?
		if ( di->m_a < 0 ) { char *xx=NULL;*xx=0; }
		// telescope up until we hit the sentence section
		Section *ss = m_sections->m_sectionPtrs[di->m_a];
		for ( ; ss ; ss = ss->m_parent ) 
			if ( ss->m_flags & SEC_SENTENCE ) break;
		// must have it
		if ( ! ss ) { char *xx=NULL;*xx=0; }
		// scan for words
		if ( isTicketDate ( ss->m_a, ss->m_b, m_wids, m_bits ,
				    m_niceness ) )
			di->m_flags |= DF_NONEVENT_DATE;
		// is it a funeral date?
		if ( isFuneralDate ( ss->m_a , ss->m_b ) )
			di->m_flags |= DF_NONEVENT_DATE;

		// if it has "since" or "by" before it, call it a ticket date
		bool flag = 0;
		for ( int32_t b = di->m_a - 1; b >= 0 ; b-- ) {
			QUICKPOLL(m_niceness);
			if ( ! m_wids[b] ) continue;
			// in business since August
			if ( m_wids[b] == h_since ) { flag=1; break; }
			// rsvp: aug 23, 2011
			if ( m_wids[b] == h_rsvp ) { flag=1; break; }
			// ... that we receive on or before Dec 5, 2011
			if ( m_wids[b] == h_before ) { flag=1; break; }
			if ( m_wids[b] == h_by    ) { flag=1; break; }
			if ( m_wids[b] == h_deadline) { flag=1; break; }
			break;
		}

		int64_t last = 0LL;
		int32_t wcount = 0;
		// similarly, scan back but don't stop at first alnum word
		for ( int32_t b = di->m_a - 1; b >= 0 ; b-- ) {
			QUICKPOLL(m_niceness);
			if ( ! m_wids[b] ) continue;
			int64_t prev = last;
			last = m_wids[b];
			// check in/out times for hotels
			if ( m_wids[b] == h_checkin  ) { flag=1; break; }
			if ( m_wids[b] == h_checkout ) { flag=1; break; }
			if ( prev == h_in && m_wids[b]==h_check){flag=1;break;}
			if ( prev == h_out&& m_wids[b]==h_check){flag=1;break;}
			if ( m_wids[b] == h_deadline ) { flag=1; break;}
			if ( m_wids[b] == h_call&&prev==h_us) break;
			if ( m_wids[b] == h_call&&prev==h_anytime) break;
			if ( m_wids[b] == h_please&&prev==h_call) break;
			// for this one we stop after 5 words
			if ( wcount++ >= 5 ) break;
		}

		if ( flag ) {
			di->m_flags |= DF_NONEVENT_DATE;
			continue;
		}

		// check the sentence section before the date for like
		// "Phone Hours:" or something. this was not the header in
		// its own section for thetripledoor.net so this fixes it. but
		// this breaks signmeup.com because the "Thanksgiving" we'd
		// like to telescope to follows a sentence with "register"
		// in it! so to fix triple door be more restrictive.
		Section *ps = NULL;
		for ( int32_t b = ss->m_a - 1; b >= 0 ; b-- ) {
			QUICKPOLL(m_niceness);
			if ( ! m_wids[b] ) continue;
			ps = m_sections->m_sectionPtrs[b];
			break;
		}
		for ( ; ps ; ps = ps->m_parent ) 
			if ( ps->m_flags & SEC_SENTENCE ) break;
		if ( ! ps ) continue;
		//if ( ps && isTicketDate ( ps->m_a, ps->m_b,m_wids,m_bits ) )
		// be more restrictive
		if ( m_wids[ps->m_a  ] == h_phone &&
		     m_wids[ps->m_a+2] == h_hours &&
		     ps->m_b == ps->m_a+3 )
			di->m_flags |= DF_NONEVENT_DATE;
	}

	//////////////////////////////
	//
	// set Date::m_headColSection and m_headRowSection. 
	//
	// these only apply to dates in a table! we need to get the 
	// corresponding headers to see if they have certain keywords in 
	// them like "closed" or "buy tickets" which would indicate dates
	// of closing or ticket sales respectively and not of events per se.
	//
	//////////////////////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if not in body (i.e. from url)
		if ( ! (di->m_flags & DF_FROM_BODY) ) continue;
		// get section
		Section *si = di->m_section;
		// must be in table
		if ( ! ( si->m_flags & SEC_IN_TABLE ) ) continue;
		// telescope until we got a valid row header
		for ( ; si ; si = si->m_parent ) {
			// stop if not in table any more
			if ( ! ( si->m_flags & SEC_IN_TABLE ) ) break;
			// only certain sections have the row/col num set
			if ( ! si->m_headColSection ) continue;
			// sanity check
			if ( ! si->m_headRowSection ){continue;}
			if ( ! si->m_headRowSection ){char*xx=NULL;*xx=0;}
			// got it
			di->m_tableCell = si;
			//di->m_headColSection = si->m_headColSection;
			//di->m_headRowSection = si->m_headRowSection;
			// must have a rolc/olnum set too!
			//if ( si->m_colNum == 0 ) { char *xx=NULL;*xx=0; }
			//if ( si->m_rowNum == 0 ) { char *xx=NULL;*xx=0; }
			//di->m_colNum = si->m_colNum;
			//di->m_rowNum = si->m_rowNum;
			// go to next date
			break;
		}
	}


	//////////////////////////////
	//
	// set DF_NONEVENT_DATE from table header
	//
	// a fix for nycday.eventbrite.com which has "sales end" in the
	// table column of the date we mistake for the event date
	//
	//////////////////////////////
	// now if the date is in a table and the table row/col header has
	// some of the words above in it, then assume the date is
	// a date for buying tickets, etc.
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// in a table?
		Section *cell = di->m_tableCell;
		// skip if not in a table
		if ( ! cell ) continue;
		// get row section
		Section *rs = cell->m_headRowSection;
		// our run num must be #1, i.e. right after row header!
		// otherwise we fail for events.mapchannels.com tingley 
		// coliseum events, which has "Buy Tickets $xx" in the first
		// column for every row...
		if ( cell->m_colNum != 2 ) 
			rs = NULL;
		// if this is not a simple little header, forget it!
		// tables can get nested and big and crap...
		if ( rs && rs->m_lastWordPos - rs->m_firstWordPos > 20 )
			rs = NULL;
		// scan for words
		if ( rs && isTicketDate ( rs->m_a , rs->m_b , m_wids , NULL ,
					  m_niceness ) )
			di->m_flags |= DF_NONEVENT_DATE;
		// get col section
		Section *cs = cell->m_headColSection;
		// if this is not a simple little header, forget it!
		// tables can get nested and big and crap...
		if ( cs->m_lastWordPos - cs->m_firstWordPos > 20 )
			cs = NULL;
		// if we are the "header" row, forget this!
		if ( cell->m_rowNum == 1 )
			cs = NULL;
		// scan for words
		if ( cs && isTicketDate ( cs->m_a , cs->m_b , m_wids , NULL ,
					  m_niceness ) )
			di->m_flags |= DF_NONEVENT_DATE;
	}		

	//
	// end moved from parseDates()
	//



	// set up the hash table for holding tag hashes, "Tag Hash Table"
	HashTableX tht; tht.set ( 4 , 4 ,0,NULL,0,false,m_niceness,"tht");

	// . hash the tag hashes of the sections of each date
	// . for telescoped dates we use the first innermost date's section
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if nullified
		if ( ! di ) continue;
		// skip if not in body - no tag hash then
		if ( ! ( di->m_flags & DF_FROM_BODY ) ) {
			di->m_occNum    = 0;
			di->m_clockHash = 0;
			continue;
		}
		// sanity check
		if ( di->m_a < 0 ) { char *xx=NULL;*xx=0; }
		// get section
		//Section *sp = m_sections->m_sectionPtrs[di->m_a];
		// int16_tcut
		//int32_t tagHash = sp->m_tagHash;
		// panic
		//if ( tagHash == 0 || tagHash == -1 ) { char *xx=NULL;*xx=0; }
		// save it
		//di->m_tagHash = tagHash;
		// get tag hash
		int32_t slot = tht.getSlot ( &di->m_tagHash );
		// assume not there
		int32_t occNum = 0;
		// if there update
		if ( slot >= 0 ) occNum=*(int32_t *)tht.getValueFromSlot(slot);
		// store it, start at 0 this time
		di->m_occNum = occNum;
		// . only set this if tagHash is valid (not -1)
		// . we should use this for voting in the "nsvt",
		// "New Section Voting Table" in Sections.cpp
		di->m_clockHash = hash32h ( di->m_tagHash , occNum );
		// increment it
		occNum++;
		// add it back incremented
		if ( slot >= 0 ) tht.setValue ( slot , &occNum );
		// . return false and set g_errno on error. 
		// . only attempt to add back if the "tagHash" is valid(not -1)
		else if ( ! tht.addKey ( &di->m_tagHash, &occNum ) ) 
			// g_errno should be set!
			return false;
	}

	// set the "is unique" flag for dates in body
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if nullified
		if ( ! di ) continue;
		// skip if not in body - no tag hash then
		if ( ! ( di->m_flags & DF_FROM_BODY ) ) continue;
		// look in the occurence table
		int32_t slot = tht.getSlot ( &di->m_tagHash );
		// must be there
		if ( slot < 0 ) { char *xx=NULL;*xx=0; }
		// get occ num
		int32_t numOccurences = *(int32_t *)tht.getValueFromSlot ( slot );
		// sanity check
		if ( numOccurences <= 0 ) { char *xx=NULL;*xx=0; }
		// 1 means unique
		if ( numOccurences == 0 ) di->m_flags |= DF_UNIQUETAGHASH;
	}

	// free the mem!
	tht.reset();

	//
	// see how much we've changed since last time we were spidered
	//
	m_changed = 0;
	// only do this if we got an old title rec
	if ( od ) {
		// if the old title rec changed significantly in the positive 
		// scoring section, then add a "modified date" to the list of 
		// pubdates

		// . get the sample vector of the new and the old doc
		// . TODO: ensure does not include dates!!!!!!!
		int32_t *s1 = nd->getPageSampleVector();
		int32_t *s2 = od->getPageSampleVector();

		// sanity check
		if ( ! s1 || ! s2 || s1 == (void *)-1 || s2 == (void *)-1 ) {
			char *xx=NULL;*xx=0; }

		// otherwise, estimate a pub date.
		int32_t t1 = od->m_spideredTime;
		int32_t t2 = nd->m_spideredTime;
		// this is strange
		if ( t1 == -1 ) { char *xx=NULL;*xx=0; }
		// bisection method
		int32_t t3 = t1 + ( (t2 - t1) / 2);
		// get how similar they are from 0 to 100
		float sim = computeSimilarity(s1,s2,NULL,NULL,NULL,m_niceness);
		// convert into percent changed
		m_changed = 100 - (int32_t)sim;
		// add "t3" as an estimated pub date if change was 20% or more
		if ( m_changed >= 20 && 
		     ! addDate (DT_TIMESTAMP,DF_ESTIMATED,-1,-1,t3))
			return false;
	}


	// 1. check ourselves with "dates" of the old title rec to see if our
	//    pub date candidates changed or not. if they changed, they are
	//    a clock, otherwise they are not a clock. set DF_CLOCK and
	//    DF_NOTCLOCK flags from this.

	// . current time. sync'd with host #0 who uses ntp supposedly...! :(
	// . to ensure that the "qatest123" subdir re-injects docs exactly the
	//   same, we need to use this date now
	int32_t now = nd->m_spideredTime; 
	// how long has elapsed since we downloaded it last approx.?
	int32_t elapsed = 0; 
	if ( od ) elapsed = now - od->m_spideredTime;
	// sanity check.
	// when a different twin downloaded this in the past, its clock
	// might have been different than ours... actually i think our
	// spiderdate.txt file had an older date in it from a previous round!
	// so disable this when test spidering.
	if ( elapsed<0 && g_conf.m_testSpiderEnabled && !strcmp(m_coll,
								"qatest123"))
		elapsed = 0;
	// is true.
	if ( elapsed < 0 ) { 
		log("date: CRAZY! elasped=%"INT32"<0",elapsed);
		elapsed = 0;
		//char *xx=NULL;*xx=0; }
	}
	// get the same date table now
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;

		// set DF_AMBIG
		if ( ( di->m_flags & DF_AMERICAN ) &&
		     ( di->m_flags & DF_EUROPEAN ) ) {
			di->m_flags |= DF_AMBIGUOUS;
			continue;
		}

		// any pub date more than 25 hours old is not a clock
		if ( now - di->m_timestamp >25*60*60 ) {
			di->m_flags |= DF_NOTCLOCK;
			continue;
		}
		// anything in future is not clock
		if ( di->m_timestamp - now > 25*60*60 ) {
			di->m_flags |= DF_NOTCLOCK;
			continue;
		}
		// anything in future is not clock
		//if ( di->m_maxDOJ == UNBOUNDED ) {
		//	di->m_flags |= DF_NOTCLOCK;
		//	continue;
		//}

		// obviously ranges and lists are not clocks
		if ( di->m_hasType & (DT_RANGE_ANY|DT_LIST_ANY) ) {
			di->m_flags |= DF_NOTCLOCK;
			continue;
		}

		// can't go beyond here without older copy
		if ( ! cct ) continue;

		// search for it in the old document
		// make the key
		int64_t key = di->m_tagHash ;
		key |= ((int64_t)di->m_occNum) << 32;
		int32_t *val = (int32_t *)cct->getValue ( &key );
		// skip if not in the old document
		if ( ! val ) continue;

		// search for it in the old document
		//int32_t j ;
		//Date *dj;
		//for ( j = 0 ; j < odp->m_numDatePtrs ; j++ ) {
		//	// get it
		//	dj = odp->m_datePtrs[j];
		//	// skip if none
		//	if ( ! dj ) continue;
		//	// skip if not it
		//	if ( dj->m_tagHash != di->m_tagHash ) continue;
		//	// must match this too!
		//	if ( dj->m_occNum != di->m_occNum ) continue;
		//	// break if we got it
		//	break;
		//}
		// skip if not in the old document
		//if ( j >= odp->m_numDatePtrs ) continue;

		// if the pub date CHANGED since last time we spidered it
		// it is probably a clock, BUT if the content also changed
		// then maybe it is just an updated date or something.
		//bool same = (di->m_timestamp == dj->m_timestamp);
		bool same = (di->m_timestamp == *val);
		// if the date changed value and the document is basically
		// the same... then consider it a clock
		if ( ! same && m_changed == 0 )
			di->m_flags |= DF_CLOCK;
		// only label it as "not a clock" if it has been more than
		// 25 hrs since we last downloaded it. in that time, if it
		// really was a clock, it should have updated. UNLESS IT
		// WAS A MONTHTLY CLOCK!!!! which we should ignore!! TODO!!!
		if ( same && elapsed > 25*60*60 ) 
			di->m_flags |= DF_NOTCLOCK;
	}

	/*
	// now unset any DF_AMIBUGOUS flags if just one date in the doc's
	// body is not ambiguous and has a numeric month
	int32_t american = 0;
	int32_t european = 0;
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) { // && odp ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get flag
		dateflags_t flags = di->m_flags;
		// must be non-ambig. only numeric dates can be ambiguous!
		if ( flags & DF_AMBIGUOUS ) continue;
		// what format are we?
		if ( flags & DF_AMERICAN ) american++;
		if ( flags & DF_EUROPEAN ) european++;
	}

	// do we have a clear indication here - must be a landfall
	m_dateFormat = 0;
	if ( american > 0 && european == 0 ) m_dateFormat = DF_AMERICAN;
	if ( european > 0 && american == 0 ) m_dateFormat = DF_EUROPEAN;
	// . if we are confused, panic!
	// . XmlDoc::getIndexCode() should abort!
	if ( american > 0 && european > 0 ) {
		log("dates: url %s has ambig ameri/euro date formats.",
		    m_url);
		reset();
		m_dateFormatPanic = true;
		return true;
	}

	// unset the ambigous flags now
	for ( int32_t i = 0 ; i < m_numDatePtrs && m_dateFormat ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get flag
		dateflags_t flags = di->m_flags;
		// must be non-ambig. only numeric dates can be ambiguous!
		if ( ! ( flags & DF_AMBIGUOUS ) ) continue;
		// it is ok now!
		di->m_flags &= ~DF_AMBIGUOUS;
		// unset the one it is not
		if ( m_dateFormat == DF_AMERICAN ) di->m_flags &= ~DF_EUROPEAN;
		if ( m_dateFormat == DF_EUROPEAN ) di->m_flags &= ~DF_AMERICAN;
	}
	*/
	// success
	return true;
}

// . extract the date from the url
// . for now must have "/2008/10/12" exactly in there as a substring to work
// . support:
//   www.charleston.net/news/2008/oct/12/teachers_criticize_test_moni...
//   www.vagazette.com/la-sp-hgh11-2008nov11,0,7671762.story 
//   developer.yahoo.net/blog/archives/2008/11/yahoo_openid_test.html 
//   www.koreatimes.co.kr/www/news/nation/2008/12/117_36346.html 
//   content-uk.cricinfo.com/ausvrsa2008_09/engine/current/match/351682.html 
//   www.archive.org/download/mogwai2008-09-06.flac/mogwai2008-09-06t09.flac 
//   www.nypost.com/seven/01042009/news/regionalnews/freak_accident_kills_man..
//   www.freep.com/article/20081220/SPORTS06/81220047/1054/SPORTS06 
//  princessofgod87.blogdrive.com/archive/cm-12_cy-2008_m-1_d-5_y-2009_o-0.html
//   www.hot001.com/index.php/htm/recruitment/2009-01-03/35061.html 
//   torrentfreak.com/the-pirate-bay-sees-traffic-and-peers-surge-081115/ 
int32_t parseDateFromUrl ( char *url         , 
			int32_t *retUrlYear  ,
			int32_t *retUrlMonth ,
			int32_t *retUrlDay   ) {

	// return 0 to indicate no date
	if ( ! url ) return 0;

	// skip to path
	char *p = getPathFast ( url );

	// assume none
	if ( retUrlYear  ) *retUrlYear  = 0;
	if ( retUrlMonth ) *retUrlMonth = 0;
	if ( retUrlDay   ) *retUrlDay   = 0;
	bool hadDate = false;

	// we use these
	int32_t urlYear  = 0;
	int32_t urlMonth = 0;
	int32_t urlDay   = 0;

	// look for stuff like "/2008/12/10" in the url itself
	for ( int32_t i = 0 ; ; i++ ) {

		// skip to next /
		while ( *p && *p != '/' ) p++;
		// skip over sequence of /'s
		while ( *p == '/' ) p++;
		// skip after the /
		if ( ! *p ) break;
		
		// . this is not a date per se, but use it to set
		//   urlYear and urlMonth to aid in selecting dates from
		//   the body!
		// . look for "2004/07/"
		if ( is_digit(p[0]) &&
		     is_digit(p[1]) &&
		     is_digit(p[2]) &&
		     is_digit(p[3]) &&
		     p[4] == '/'    &&
		     is_digit(p[5]) &&
		     is_digit(p[6]) &&
		     p[7] == '/'   ) {
			int32_t y = atol2(p  ,4);
			int32_t m = atol2(p+5,2);
			if ( y > 1980 && y < 2050 &&
			     m > 0    && m < 13   ) {
				hadDate  = true;
				urlYear  = y;
				urlMonth = m;
			}
		}
		     

		// check for "/20081220/"
		if ( is_digit(p[0]) &&
		     is_digit(p[1]) &&
		     is_digit(p[2]) &&
		     is_digit(p[3]) &&
		     is_digit(p[4]) &&
		     is_digit(p[5]) &&
		     is_digit(p[6]) &&
		     is_digit(p[7]) &&
		     !is_digit(p[8]) ) {
			int32_t y = atol2(p  ,4);
			int32_t m = atol2(p+4,2);
			int32_t d = atol2(p+6,2);
			if ( y > 1980 && y < 2050 &&
			     m > 0    && m < 13   &&
			     d > 0    && d < 32    ) {
				hadDate = true;
				urlYear  = y;
				urlMonth = m;
				urlDay   = d;
				break;
			}
		}

		// "/01042009/"
		if ( is_digit(p[0]) &&
		     is_digit(p[1]) &&
		     is_digit(p[2]) &&
		     is_digit(p[3]) &&
		     is_digit(p[4]) &&
		     is_digit(p[5]) &&
		     is_digit(p[6]) &&
		     is_digit(p[7]) &&
		     !is_digit(p[8]) ) {
			int32_t m = atol2(p  ,2);
			int32_t d = atol2(p+2,2);
			int32_t y = atol2(p+4,4);
			if ( y > 1980 && y < 2050 &&
			     m > 0    && m < 13   &&
			     d > 0    && d < 32    ) {
				hadDate = true;
				urlYear  = y;
				urlMonth = m;
				urlDay   = d;
				break;
			}
		}
	
		// 2004-Sep-23
		// 2004/Sep/23
		if ( ! is_digit(p[0]) ) continue;
		if ( ! is_digit(p[1]) ) continue;
		if ( ! is_digit(p[2]) ) continue;
		if ( ! is_digit(p[3]) ) continue;
		if (   is_alnum_a(p[4]) ) continue;
		// p == "2010\0" ?
		if ( ! p[4] ) continue;
		// must be in range
		int32_t year = atol2(p,4);
		// we need to support all dates in 1900+ i guess
		if ( year < 1900 || year > 2032 ) continue;
		
		// skip year, point to possible month
		char *m = p + 5;
		// fix p = "2010/\0" case
		if ( ! *m ) continue;
		// find end of it
		char *mend = m + 1;
		while ( *mend && ! is_punct_a ( *mend ) ) mend++;
		// must not be end, we need a day to follow
		if ( ! *mend ) continue;
		// make it a wid
		int64_t wid = hash64Lower_utf8 ( m , mend - m , 0LL );
		// get it as a month
		int32_t month = getMonth ( wid ) ;
		// skip if not valid month
		if ( month <= 0 || month > 12 ) continue;

		// get the day component
		char *d = mend + 1;
		// must be there
		if ( ! *d ) continue;
		// skip if not numeric
		char *dend = d + 1;
		if ( is_digit ( *dend ) ) dend++;
		if ( is_alnum_a ( *dend ) ) continue;
		// must be in range
		int32_t day = atol2(d,dend-d);
		if ( day <= 0 || day > 31 ) continue;
		// save it
		hadDate = true;
		urlYear  = year;
		urlMonth = month;
		urlDay   = day;
		// all done
		break;
	}
	// couldn't get one?
	if ( ! hadDate ) return 0;
	// copy over if caller wants them
	if ( retUrlYear  ) *retUrlYear  = urlYear;
	if ( retUrlMonth ) *retUrlMonth = urlMonth;
	if ( retUrlDay   ) *retUrlDay   = urlDay;
	// convert to timestamp in seconds since the epoch
	tm ts1;
	memset(&ts1, 0, sizeof(tm));
	ts1.tm_mon  = urlMonth - 1;
	ts1.tm_mday = urlDay;
	ts1.tm_year = urlYear - 1900;
	// use noon as time of day (tod)
	ts1.tm_hour = 12;
	// . make the time
	// . this is -1 for early years!
	time_t timestamp = mktime(&ts1);
	// return that
	return timestamp;
}

// is this punct word suitable for being
// only allow certain punct between the month and day
// specifically a / or - or . or | or 
bool Dates::checkPunct ( int32_t i , Words *words , char *singleChar ) {

	*singleChar = '\0';

	// return false if not there
	if ( i < 0                    ) return false;
	if ( i >= words->m_numWords ) return false;
	// if tag say false
	if ( words->m_tagIds[i] ) return false;
	// or alnum return false too
	if ( words->m_wordIds[i] ) return false;

	char *pp     =      words->m_words[i];
	char *ppend  = pp + words->m_wordLens[i];
	char  pcount = 0;

	for ( ; pp < ppend ; pp++ ) {
		// skip if good
		if ( is_wspace_a(*pp) ) continue;
		if ( ++pcount >= 2 ) break;
		if ( *pp == '/'  ) { *singleChar = '/' ; continue; }
		if ( *pp == '\\' ) { *singleChar = '\\' ; continue; }
		if ( *pp == '-'  ) { *singleChar = '-' ; continue; }
		if ( *pp == '.'  ) { *singleChar = '.' ; continue; }
		if ( *pp == '|'  ) { *singleChar = '|' ; continue; }
		break;
	}
	return ( pp >= ppend );
}


// format:
// Oct 12, 1999 MST
// October 12, 1999 MST
// Oct. 12, 1999 MST
// 10/12/1999 GMT
// 12/10/1999 UTC (european)
// 12 Oct 1999 MST

// month-day-year format (american)
// {Oct|Oct.|October|10}{ |/|.} {1-31[th|nd|rd|st]} { |,|/|.|'} 2008[EST|UTC]
// day-month-year format (european)
// {1-31[th|nd|rd|st]} {Oct|Oct.|October|10}{ |/|.} { |,|/|.|'} 2008[EST|UTC]

// examples:
// October 28, 2008 10:03 AM
// Wednesday, December 17, 2008  .... content .... 3:44 PM   
// December 26, 2008
// 02 October, 2008 12:05 PM EST 
// 2009-01-07 12:07:20
// Posted on November 3, 2008 ... Posted at 11:38 pm
// November 04 2008 @ 5:33 pm
// December 29, 2008 - 4:23 pm PDT
// 8:03 pm   -   October 23rd, 2008 
// October 23rd, 2008 at 10:01 pm
// Posted by Lee Odden on Dec 12th, 2008 in ...
// December 12, 2008 at 6:23 am
// Date: October 28th, 2008
// 10/28/08
// by Erick Schonfeld on October 8, 2008 
// October 8th, 2008 at 9:11 am PDT
// October 21st, 2008 
// Dec 30 2008
// posted: Jan 9, 2009
// Dec 2, 2008
// By Doug Caverly - Wed, 11/12/2008 - 4:02pm. 
// Submitted by Cari (WPN reader) on Thu, 11/13/2008 - 7:31pm.
// Posted by Drew McLellan on December 1, 2008 in Guest posts  | Permalink
// Filed as News on October 31, 2008 5:03 pm
// Posted at 9:40 PM PT on November 29, 2008
// Posted by Puneet Thapliyal  at November 29th, 2008 at 9:55 pm 
// Last updated December 31 2008: 10:36 AM ET
// December 22nd, 2008  ... Posted by Larry Dignan @ 2:50 am
// Posted by: rokemronnie Posted on: 12/23/08
// Thursday, November 13, 2008
// Published: 18/12/2008 18:01
// Posted by: Trainee at CC on 13 Dec 2007
// Posted by Ben Johnson on Wed, Dec 5, 2001 at 1:00am
// 2008-12-24
// Sunday, December 21, 2008 4:56:00 PM 
// october twentieth 2008
// october twentieth two thousand and eight
// Posted by TheAfroBeat at 11/09/2008 10:15:00 PM 
// Posted by Dustin on November 6, 2007   |  Comments (3) 
// 11.10.2008  14:30
// Posted by PDX.rb 1129 days ago
// kern2869el ([info]kern2869el) wrote,@ 2007-09-16 14:24:00
// 28th-Nov-2007 11:03 pm
// July 17, 2005 in Santa Barbara  | Permalink  |  Comments (2)  
// 7:11 AM Jan 3rd from TwitterFox
// 5.5.2008
// Posted on 5.5.2008 at 6:20 AM
// It.s News To M_A - 12/29/08 ... December 29th, 2008
// 07.16.08

// SUPPORTED
// . 2 Apr - Wed 10:00AM-10:45AM
// . Thu, 11/15/07 - Fri, 4/11/08
// . Monday <tag> November 23, 2009 - November 27, 2009
// . November 1st - 29th, 2009
// . from 11/06/09 to 11/18/09
// . Sat., Apr. 05, 9:00 AM Registration/Preview, 10:00 AM Live Auction
// . Saturday, November 21, 2009 .... -- A Christmas bazaar begins at 9 a.m. 


// UNSUPPORTED (TODO!)
// . 9pm 6th-18th of novemeber. (use new multi-type ranges logic)
// . 5th, 6th and 8th of november (array of lia[] - numbers on left)
// . nov 9th and 30th and 31st (array of ria[] - numbers on right)
// . dec 2, 3 and 7th and jan 9,11,12, 20 2009 at 7pm (array of ria[])
// . nov 9th to 30th (use ranges logic for this one!)
// . starting at nine each night 
// . Weekdays 8:30am-4:30pm   (use new "wday" logic - day bit vector)
// . Every Tuesday of the month from 10:00-11:00 a.m (use new "wday" logic)
// . 2nd Saturday of every month, 10:00 am-12:00 pm (use new "wday" logic)
// . tomorrow at 8pm (use "wday" logic - relative to pubdate!)
// . tuesday at 8pm (use "wday" logic - relative to pubdate!)
// . two days from tomorrow (use "wday" logic - relative to pubdate!)
// . two hours before noon (use "wday" logic - relative to pubdate!)
// . <h1>2009</h1><h2>NOVEMBER</h2> ... <b>12th</b> ... (use inheritance)
// . calendar format where isolated numbers from 1 to 31 in table cells.
//   like in burtstiki.com calendar
// . start is current, only end is specified:
//   "The group runs through December 28"
//   "The lecture series closes November ninth"
// . "at five thirty p.m." (numbers as words)
// . "Meeting Nov. 5, 2009-10-07" (make sure that works!)
// . "2 1/2 blocks east" (watch out for false positives)
// . "tuna christmas page". some dates should not inherit from others. test!
//   http://10.5.1.203:8000/test/doc.5760726932125344521.html
// . "WEDNESDAY (18th)" (isolated numeric day of month, only a few on page,
//   but the current date/clock in on the page)
// . "tues-fri 8am-5pm" (uses wday logic)
// . <h1>12/10/09</> <td>10pm</><td>11pm</td> (inherit dates to the tods)
// . "runs through 1/11" (http://alibi.com/index.php?scn=cal)
// . "ongoing through 1/11, every monday and tuesday, 6pm - 9pm"
// . "every thanksgiving at 2pm"
// . "the day before president's day"


// . http://alibi.com/index.php?scn=cal :
//   "Runs thru 1/10".
// . http://guildcinema.com/ :
//    "nov 23 to nov 27, monday through friday 4:00, 6:00, 8:00"
// . http://alibi.com/index.php?scn=cal :
//    "nov. 20 through nov. 27"
// . http://www.santafeplayhouse.org/onstage.php4 :
//    "December 10, 2009 - January 3, 2010"
//    Dec 10, 17, at 8:00pm
//    Dec 13, 20, 27, Jan 3, at 2:00pm
//    "ongoing through 1/11, every monday and tuesday, 6pm - 9pm"


// . "nov 23 to dec 27, monday through friday 4:00, 6:00, 8:00 MDT"


static bool s_init42 = false;

// returns false with g_errno set on error
bool Dates::parseDates ( Words *w , dateflags_t defFlags , Bits *bits ,
			 Sections *sections , int32_t niceness ,
			 Url *url ,
			 uint8_t ctype ) {

	m_nw = w->getNumWords();
	m_niceness = niceness;

	m_calledParseDates = true;

	// now Sections class calls parseDates() independently so it can 
	// identify implied sections based on dow/dom and tod date pairs.
	// this allows us to corral tods with possible dow/dom headers.
	// so when XmlDoc calls Dates::setPart1() we do not want to re-do this!
	if ( defFlags == DF_FROM_BODY ) {
		// sanity check
		if ( m_bodySet ) return true;
		// mark it as set now
		m_bodySet = true;
	}

	// int16_tcuts
	if ( ! s_init42 ) {
		// only do once
		s_init42 = true;

		h_funeral = hash64n ("funeral");
		h_mortuary = hash64n ("mortuary");
		h_visitation = hash64n ("visitation");
		h_memorial = hash64n ("memorial");
		h_services = hash64n ("services");
		h_service  = hash64n ("service");
		h_founded = hash64n("founded");
		h_established = hash64n("established");

		h_seniors = hash64n("seniors");
		h_a = hash64n("a");
		h_daily = hash64n("daily");
		h_sunday = hash64n("sunday");
		h_monday = hash64n("monday");
		h_tuesday = hash64n("tuesday");
		h_wednesday = hash64n("wednesday");
		h_thursday = hash64n("thursday");
		h_friday = hash64n("friday");
		h_saturday = hash64n("saturday");
		h_mon = hash64n("mon");
		h_tues = hash64n("tues");
		h_tue = hash64n("tue");
		h_wed = hash64n("wed");
		h_wednes = hash64n("wednes");
		h_thurs = hash64n("thurs");
		h_thu = hash64n("thu");
		h_thr = hash64n("thr");
		h_fri = hash64n("fri");
		h_sat = hash64n("sat");

		h_s        = hash64n("s");
		h_noon     = hash64b("noon");
		h_midnight = hash64b("midnight");
		h_midday   = hash64b("midday");
		h_dawn     = hash64n("dawn");
		h_sunrise  = hash64n("sunrise");
		h_dusk     = hash64n("dusk");
		h_sunset   = hash64n("sunset");
		h_sundown  = hash64n("sundown");

		h_last     = hash64n("last");
		h_modified = hash64n("modified");
		h_posted   = hash64n("posted");
		h_updated  = hash64n("updated");
		h_by       = hash64n("by");
		h_festival = hash64n("festival");

		// set these guys
		h_to         = hash64b("to");
		h_and        = hash64b("and");
		h_or         = hash64b("or");
		h_sun        = hash64b("sun");
		h_next       = hash64n("next");
		h_this       = hash64n("this");
		h_children   = hash64n("children");
		h_age        = hash64n("age");
		h_ages       = hash64n("ages");
		h_kids       = hash64n("kids");
		h_toddlers   = hash64n("toddlers");
		h_youngsters = hash64n("youngsters");
		h_grade      = hash64n("grade");
		h_grades     = hash64n("grades");
		h_day        = hash64n("day");
		h_years      = hash64n("years");
		h_continuing = hash64b("continuing");
		h_through    = hash64b("through");
		h_though    = hash64b("though"); // misspelling
		h_thru       = hash64b("thru");
		h_until      = hash64b("until");
		h_til        = hash64b("til");
		h_till       = hash64b("till");
		h_ongoing    = hash64b("ongoing");
		h_runs       = hash64b("runs");
		h_results    = hash64b("results");
		h_nightly    = hash64n("nightly");
		h_lasts      = hash64b("lasts");
		h_lasting    = hash64b("lasting");
		h_at         = hash64b("at");
		h_on         = hash64b("on");
		h_starts     = hash64b("starts");
		h_begins     = hash64b("begins");
		h_between    = hash64b("between");
		h_closed     = hash64n("closed");
		h_closure    = hash64n("closure");
		h_closures   = hash64n("closures");
		h_from       = hash64b("from");
		h_before     = hash64b("before");
		h_after      = hash64b("after");
		h_ends       = hash64b("ends");
		h_conclude   = hash64n("conclude");
		h_concludes  = hash64n("concludes");
		h_date       = hash64n("date");
		h_time       = hash64n("time");
		h_the        = hash64b("the");
		h_copyright  = hash64b("copyright");

		// 1<sup>st</sup> ...
		h_st = hash64n("st");
		h_nd = hash64n("nd");
		h_rd = hash64n("rd");
		h_th = hash64n("th");
		
		h_sundays    = hash64n("sundays");
		h_mondays    = hash64n("mondays");
		h_tuesdays   = hash64n("tuesdays");
		h_wednesdays = hash64n("wednesdays");
		h_thursdays  = hash64n("thursdays");
		h_fridays    = hash64n("fridays");
		h_saturdays  = hash64n("saturdays");
		
		h_summers = hash64n("summers");
		h_autumns = hash64n("autumns");
		h_winters = hash64n("winters");

		h_details = hash64n("details");
		h_more    = hash64n("more");

		// [on] 
		// [the|each|every] 
		// [first|second|third|fourth|fifth]
		// **[sun|mon|tues|wed|thu|fri|sat]
		// [of]
		// [the|each|every] 
		// [year|month|week|season|quarter|semester]
		h_non        = hash64b("non");
		h_mid        = hash64b("mid");
		h_each       = hash64b("each");
		h_every      = hash64b("every");
		h_first      = hash64b("first");
		h_second     = hash64b("second");
		h_third      = hash64b("third");
		h_fourth     = hash64b("fourth");
		h_fifth      = hash64b("fifth");
		h_1st        = hash64b("1st");
		h_2nd        = hash64b("2nd");
		h_3rd        = hash64b("3rd");
		h_4th        = hash64b("4th");
		h_5th        = hash64b("5th");
		h_1          = hash64b("1");
		h_2          = hash64b("2");
		h_3          = hash64b("3");
		h_4          = hash64b("4");
		h_5          = hash64b("5");
		h_of         = hash64b("of");
		h_year       = hash64b("year");
		h_month      = hash64b("month");
		h_week       = hash64b("week");

		h_weeks      = hash64b("weeks");
		h_days       = hash64b("days");
		h_months     = hash64b("months");
		h_miles      = hash64b("miles");
		h_mile       = hash64b("mile");
		h_mi         = hash64b("mi");
		h_km         = hash64b("km");
		h_kilometers = hash64b("kilometers");
		h_kilometer  = hash64b("kilometer");

		h_night      = hash64b("night");
		h_nights     = hash64b("nights");
		h_evening    = hash64b("evening");
		h_evenings   = hash64b("evenings");
		h_morning    = hash64b("morning");
		h_mornings   = hash64b("mornings");
		h_afternoon  = hash64b("afternoon");
		h_afternoons = hash64b("afternoons");

		h_in = hash64n("in");
		h_hours = hash64n("hours");
		h_are = hash64n("are");
		h_is  = hash64n("is");
		h_semester = hash64n("semester");
		h_box = hash64n("box");
		h_office = hash64n("office");
		h_during = hash64n("during");
		h_desk = hash64n("desk");
		h_reception = hash64n("reception");

		h_register = hash64n("register");
		h_registration = hash64n("registration");
		h_phone = hash64n("phone");
		h_please = hash64n("please");
		h_call = hash64n("call");
		h_us = hash64n("us");
		h_anytime = hash64n("anytime");
		h_be = hash64n("be");
		h_will = hash64n("will");
		h_sign = hash64n("sign");
		h_up = hash64n("up");
		h_signup = hash64n("signup");
		h_tickets = hash64n("tickets");
		h_advance = hash64n("advance");
		h_purchase = hash64n("purchase");
		h_get = hash64n("get");
		h_enroll = hash64n("enroll");
		h_buy = hash64n("buy");
		h_presale = hash64n("presale");
		h_pre = hash64n("pre");
		h_sale = hash64n("sale");
		//h_on = hash64n("on");
		h_sales = hash64n("sales");
		h_end   = hash64n("end");
		h_begin = hash64n("begin");
		h_start = hash64n("start");

		//h_closed = hash64n("closed");
		h_closes = hash64n("closes");
		h_close  = hash64n("close");
		h_except = hash64n("except");
		h_open   = hash64n("open");
		h_opens  = hash64n("opens");

		h_happy = hash64n("happy");
		h_kitchen= hash64n("kitchen");
		h_hour= hash64n("hour");

		h_m = hash64n("m");
		h_mo = hash64n("mo");
		h_f = hash64n("f");

		h_late = hash64n("late");
		h_early = hash64n("early");

		h_since = hash64n("since");
		h_rsvp = hash64n("rsvp");
		h_checkin = hash64n("checkin");
		h_checkout = hash64n("checkout");
		h_check = hash64n("check");
		h_out = hash64n("out");
		h_deadline = hash64n("deadline");
		h_am = hash64n("am");
		h_pm = hash64n("pm");
	}


	if ( ! w ) return true;

	int64_t   *wids  = w->getWordIds  ();
	int32_t           nw  = w->getNumWords ();
	char       **wptrs = w->getWords    ();
	int32_t        *wlens = w->getWordLens ();
	Words       *words = w;

	// the div style display none tags are SEC_HIDDEN now
	//sec_t badFlags =SEC_MARQUEE|SEC_STYLE|SEC_SCRIPT|SEC_SELECT|
	//SEC_HIDDEN|SEC_NOSCRIPT;

	// standard
	//dateflags_t df = defFlags; // DF_FROM_BODY;

	// need D_IS_IN_URL bits to be valid
	if ( bits ) bits->setInUrlBits ( m_niceness );
	// get bits array
	wbit_t *bb = NULL;
	// "bits" is NULL if being called from Msg13.cpp for speed purposes
	if ( bits ) bb = bits->m_bits;

	bool isFacebook = false;
	bool isTrumba   = false;
	bool isStubHub  = false;
	bool isEventBrite = false;
	if ( url && ctype == CT_XML ) {
		int32_t dlen = url->getDomainLen();
		char *dom = url->getDomain();
		if ( dlen == 12 && strncmp ( dom , "facebook.com" , 12 ) == 0 )
			isFacebook = true;
		if ( dlen == 10 && strncmp ( dom , "trumba.com" , 10 ) == 0 )
			isTrumba = true;
		if ( dlen == 11 && strncmp ( dom , "stubhub.com" , 11 ) == 0 )
			isStubHub = true;
		if ( dlen == 14 && strncmp ( dom , "eventbrite.com" , 14)== 0 )
			isEventBrite = true;
	}


	/////////////////////////////////
	// 
	// part 1 - SET BASIC TYPES
	//
	// add the following numerical date formats as MONTH/DAY/YEAR types:
	//
	//   1/23/09    (american)
	//   23/01/09   (european)
	//   2009/12/03 (year-first format)
	//   1/10/09
	//   1/10/2009
	//   1.10.09
	//   1-10-09
	//   1 / 10 / 09
	//   1 / 10 / 2009
	//   1 . 10 . 09
	//   1 - 10 - 09
	//
	// AND add the following other date types:
	//
	//   2. add isolated days of week (monday,thu)
	//   3. add isolated tods (not ranges yet)
	//   4. add isolated years (convert "sixteen" to number)
	//   5. add isolated canonical months (nov,october)
	//   6. add isolated days (convert "sixteenth" to number)
	//
	/////////////////////////////////

	int64_t prevWid = 0;
	// simple flag
	bool monthPreceeds = false;
	// ignore trumba <xCal>someDate</xCal> tags, they are junky
	//bool ignoreLastTag = false;
	// int16_tcut
	nodeid_t    *tids  = w->m_tagIds;
	// for supporting "m-f" logic below
	int32_t mondayFlag = -10;
	bool inStrike = false;

	for ( int32_t i = 0 ; i < nw ; i++ ) {

		// breathe
		QUICKPOLL ( m_niceness );

		// do not breach
		//if ( m_numDatePtrs >= MAX_DATES ) break;

		//
		// TODO: allow for "8th" as isolated day (i.e. "nov 8th")
		// JUST hash "8th" into the table and map it to 8! like
		// how we map "Sunday" to 1.
		// actually i mean "eighth" in this case!!
		//

		/*
		  we detect pub date tags below and set DF_PUB_DATE, so
		  we should not need this logic. plus we end up using these
		  pubdates as event titles, so we need to set the D_IS_IN_DATE
		  bits for them so that doesn't happen for trumba.com. 

		if ( tids[i] ) {
			// turn this off
			ignoreLastTag = false;
			// skip if not xml node
			if ( tids[i] != TAG_XMLTAG ) continue;
			// check name for trumba <xCal*> tag
			if ( wptrs[i][1] == 'x'  &&
			     wptrs[i][2] == 'C'  &&
			     wptrs[i][3] == 'a'  &&
			     wptrs[i][4] == 'l'  )
				ignoreLastTag = true;
			// ignore "<x-trumba:local*" tags
			if ( wptrs[i][1] == 'x'  &&
			     !strncasecmp(wptrs[i]+1,"x-trumba:local",14))
				ignoreLastTag = true;
			// ignore <pubDate> for trumba
			if ( wptrs[i][1] == 'p'  &&
			     wptrs[i][2] == 'u'  &&
			     wptrs[i][3] == 'b'  &&
			     wptrs[i][4] == 'D'  )
				ignoreLastTag = true;
			// ignore <category>
			if ( wptrs[i][1] == 'c'  &&
			     wptrs[i][2] == 'a'  &&
			     wptrs[i][3] == 't'  &&
			     wptrs[i][4] == 'e'  )
				ignoreLastTag = true;
		}
		*/

		// ignore dates in <strike> sections
		if ( tids[i] ) {
			// are we in a strike tag?
			if      ( tids[i]==TAG_STRIKE          )inStrike=true;
			else if ( tids[i]==(TAG_STRIKE|BACKBIT))inStrike=false;
			else if ( tids[i]==TAG_S               )inStrike=true;
			else if ( tids[i]==(TAG_S|BACKBIT)     )inStrike=false;
			continue;
		}

		// skip if punct or tag
		if ( ! wids[i] ) continue;

		// skip if ignoring
		if ( inStrike ) 
			continue;

		// ignore trumba <xCal*> tags
		//if ( ignoreLastTag ) continue;

		// get the section
		//Section *sn = m_sections->m_sectionPtrs[i];
		// get the section's flags
		//sec_t flags = sn->m_flags;
		// skip if in script tag, etc.
		//if ( flags & badFlags ) continue;


		// save this
		int64_t savedWid = prevWid;
		// and update
		prevWid = wids[i];
		// getDateType() needs this
		m_wptrs = words->m_words;

		bool saved = monthPreceeds;
		// flip it off
		monthPreceeds = false;

		// deal with date types that do not start with a digit
		if ( ! is_digit(wptrs[i][0]) ) {
			// int16_tcut
			int32_t val;
			// sometimes it can be 2+ words
			int32_t endWord ;

			// . skip all dates but <start_time> for facebook
			// . no, we need to label them at least so "at 1pm" 
			//   does not think its talking about a location!
			//if ( isFacebook ) continue;

			// get type of date, if any
			datetype_t dt = getDateType ( i, &val , &endWord,
						      wids , nw ,
						      // onPreceeds?
						      savedWid == h_on );
			// skip if not recognized
			if ( ! dt ) continue;

			// if it was a month and the word "late" or "early"
			// preceeds it like "late January" then just ignore
			// it completely because it is too ambiguous and
			// not a well defined event.
			// fixes santafe.org/perl/page.cgi?p=maps;gid=2415 
			// which had "late January" splitting an event's
			// tod and day of month.
			if ( dt == DT_MONTH && 
			     ( savedWid == h_late || savedWid == h_early ))
				continue;

			// check for m-f
			if ( wids[i] == h_m || wids[i] == h_mo ) {
				// must have something after it
				if ( i+2 >= nw ) continue;
				// a hyphen next
				if ( ! words->hasChar(i+1,'-') ) continue;
				// then a friday 'f'
				if ( wids[i+2] == h_f     ) mondayFlag = i;
				if ( wids[i+2] == h_th    ) mondayFlag = i;
				if ( wids[i+2] == h_tue   ) mondayFlag = i;
				if ( wids[i+2] == h_tues  ) mondayFlag = i;
				if ( wids[i+2] == h_wed   ) mondayFlag = i;
				if ( wids[i+2] == h_thu   ) mondayFlag = i;
				if ( wids[i+2] == h_thr   ) mondayFlag = i;
				if ( wids[i+2] == h_thurs ) mondayFlag = i;
				if ( wids[i+2] == h_fri   ) mondayFlag = i;
				if ( wids[i+2] == h_sat   ) mondayFlag = i;
				if ( wids[i+2] == h_sun   ) mondayFlag = i;
				if ( wids[i+2] == h_tuesday   ) mondayFlag = i;
				if ( wids[i+2] == h_wednesday ) mondayFlag = i;
				if ( wids[i+2] == h_thursday  ) mondayFlag = i;
				if ( wids[i+2] == h_friday    ) mondayFlag = i;
				if ( wids[i+2] == h_saturday  ) mondayFlag = i;
				if ( wids[i+2] == h_sunday    ) mondayFlag = i;
				// flag
				//mondayFlag = i;
			}
			if ( wids[i] == h_f && mondayFlag != i - 2 ) 
				continue;
			if ( wids[i] == h_th && mondayFlag != i - 2 ) 
				continue;
				

			// use this now
			Date *DD;
			DD = addDate(dt,defFlags|DF_CANONICAL,
				     //left,right,
				     i,endWord,
				     val );
			if ( ! DD ) return false;

			// set SF_PLURAL in the supplemental flags
			if ( to_lower_a(wptrs[i][wlens[i]-1])=='s' &&
			     // no longer count "Friday's" as Fridays though
			     // to fix albertcadabra.com
			     wptrs[i][wlens[i]-2] != '\'' &&
			     // fix "tues". wednes thurs
			     wids[i] != h_tues &&
			     wids[i] != h_wednes &&
			     wids[i] != h_thurs )
				DD->m_suppFlags |= SF_PLURAL;

			// fix for "on christmas" (onpreceeds)
			if ( savedWid == h_on )
				DD->m_suppFlags |= SF_ON_PRECEEDS;

			// special tod? sunset sunrise dawn midnight noon...
			if ( DD->m_type == DT_TOD )
				DD->m_suppFlags |= SF_SPECIAL_TOD;

			// . set SF_HOLIDAY_WORD
			// . i don't like events to start with this base date
			//   when telescoping because it is unclear exactly
			//   what holidays are being referred to!
			//if ( dt == DT_HOLIDAY && val == HD_HOLIDAYS )
			//	DD->m_suppFlags |= SF_HOLIDAY_WORD;

			// set this
			if ( dt == DT_MONTH ) monthPreceeds = true;

			// do not re-include words from this type in the
			// next date type!
			//i = right - 1;
			i = endWord - 1;

			// that is it
			continue;
		}


		// . facebook's <start_time>1329075000</start_time>
		// . TODO: support any time_t timestamps here as well, not
		//         just facebook's tagname
		if ( i > 0 && tids[i-1] == TAG_FBSTARTTIME &&
		     // their json format uses shit like
		     // 2012-10-23T19:30:00 (like trumba)
		     wlens[i] >= 10 ) {
			// verysimple
			int32_t fbtimestamp = atol(wptrs[i]);
			// make the timestamp date
			int32_t ni = m_numDatePtrs;
			Date *di = addDate(DT_TIMESTAMP,
					   DF_OFFICIAL|DF_FROM_BODY,
					   i,i+1,fbtimestamp);
			if ( ! di ) return false;
			// make a compound date
			Date *DD1 = addDate ( DT_COMPOUND,
					      DF_OFFICIAL|DF_FROM_BODY,
					      i,i+1,fbtimestamp);
			if ( ! DD1 ) return false;
			// and add as ptr so logic works
			DD1->addPtr ( di , ni , this );

			//
			// scan for end time and make a range if there
			//
			int32_t j = i;
			int32_t maxj = j + 10;
			if ( maxj > nw ) maxj = nw; 
			for ( ; j < maxj ; j++ ) 
				if ( tids[j] == TAG_FBENDTIME ) break;
			// all done if not there. there was no end time
			if ( j == maxj ) continue;
			// skip that tag
			j++;
			// breach?
			if ( j >= nw ) continue;
			// skip until digit
			if ( ! wids[j] ) j++;
			// breach?
			if ( j >= nw ) continue;
			// must be number now
			if ( ! wids[j] ) continue;
			// there was an end time, add it
			int32_t fbtimestamp2 = atol(wptrs[j]);
			// crazy? needs to be bigger than previous timestamp
			if ( fbtimestamp2 < fbtimestamp ) continue;
			// make the timestamp date
			int32_t nj = m_numDatePtrs;
			Date *dj = addDate(DT_TIMESTAMP,
					   DF_OFFICIAL|DF_FROM_BODY,
					   j,j+1,fbtimestamp2);
			if ( ! dj ) return false;
			// make a compound date
			Date *DD2 = addDate ( DT_COMPOUND,
					      DF_OFFICIAL|DF_FROM_BODY,
					      j,j+1,fbtimestamp2);
			if ( ! DD2 ) return false;
			// and add as ptr so logic works
			DD2->addPtr ( dj , nj , this );

			//
			// make the range now
			//
			Date *DD3 = addDate ( DT_RANGE,
					      DF_OFFICIAL|DF_FROM_BODY,
					      i,j+1,0);
			if ( ! DD3 ) return false;
			// and add as ptr so logic works
			DD3->addPtr ( DD1, ni , this );
			DD3->addPtr ( DD2, nj , this );
			
			continue;
		}

		// . skip all dates but <start_time> for facebook
		// . no, we need to label them at least so "at 1pm" 
		//   does not think its talking about a location!
		//if ( isFacebook ) continue;

		//
		// no % or $ near the num!!!
		if ( i > 0 ) {
			if ( wptrs[i][-1]=='$' ) continue;
			// fix $ 20.00 for salsapower.com
			if ( wlens[i-1]>= 2 &&
			     words->hasChar(i-1,'$') )
				continue;
			// . sometimes a time duration in M:S like 38:10
			// . http://www.770kob.com/article.asp?id=521586
			// . this breaks "Sunday: 1-5 PM" on cabq.gov page
			if ( wptrs[i][-1]==':' && 
			     i>=2 &&
			     is_digit(wptrs[i][-2]) ) 
				continue;
		}
		if ( i - 3 >= 0 ) {
			// no hyphen like I-25
			// no! this messes up "07:00 am-06:00 pm"
			// crap it also messes up "<!-- ... -->01-Jul-2009"
			// so let's really specialize it to "I-25" then!
			if ( wptrs[i][-1]=='-' &&
			     wptrs[i][-2]=='I' &&
			     is_punct_a(wptrs[i][-3]) )
				continue;
		}
		// fix 3-day for cfa.aiany.org
		if ( i + 3 < nw ) {
			if ( wptrs[i+1][0]=='-' &&
			     wids[i+2] == h_day )
				continue;
		}

		if ( i+1 < nw ) {
			if ( wptrs[i+1][0]=='%' ) continue;
		}
		if ( i+2 < nw ) {
			// . or comma numbers like 7,000
			// . http://www.770kob.com/article.asp?id=521586
			if ( i > 0 &&
			     wptrs[i+1][0]==',' &&
			     is_digit(wptrs[i+1][1]) &&
			     wlens[i+2]==3 ) {
				// skip that guy 
				i += 2;
				continue;
			}
			// . skip decimal numbers like 5.50
			// . no! fix ceder.net's "6:30 - 8.00PM" fuck up
			/*
			if ( i > 0 &&
			     wptrs[i+1][0]=='.' &&
			     wids[i+2] && 
			     is_digit(wptrs[i+2][0]) &&
			     wptrs[i][-1] !='.' ) {
				// skip that guy 
				i += 2;
				continue;
			}
			*/
		}

		// get the hour
		int32_t num = w->getAsLong(i);
		// skip if crazy useless
		if ( num > 2050 ) continue;

		// if "age" or "ages" preceeds in same sentence then its
		// an age! fixes downtowndancecorvallis.com
		int32_t kmax = i - 15;
		if ( kmax < 0 ) kmax = 0;
		bool isAge = false;
		// never if am/pm follows the number (8am/8pm/10pm)
		int32_t wlen = wlens[i];
		if ( wlen>=2 && to_lower_a(wptrs[i][wlen-1]=='m'))kmax=i;
		if ( wlen>=2 && to_lower_a(wptrs[i][wlen-1]=='p'))kmax=i; //8p
		if ( wlen>=2 && to_lower_a(wptrs[i][wlen-1]=='a'))kmax=i; //8a
		// or next word is am/pm
		if ( i+2<nw && wids[i+2]==h_am ) kmax = i;
		if ( i+2<nw && wids[i+2]==h_pm ) kmax = i;
		// or a colon then number follows
		if ( i+2<nw && words->hasChar(i+1,':') &&
		     words->m_wordLens[i+2] == 2 &&
		     words->isNum(i+2) )
			kmax = i;
		// ok, scan for age inidicators
		for ( int32_t k = i - 1; k >= kmax ; k-- ) {
			if ( ! wids[k] ) continue;
			// skip if number
			if ( is_digit(wptrs[k][0]) ) continue;
			if ( wids[k] == h_age      ) isAge = true;
			if ( wids[k] == h_ages     ) isAge = true;
			if ( wids[k] == h_children ) isAge = true;
			if ( wids[k] == h_kids     ) isAge = true;
			if ( wids[k] == h_toddlers ) isAge = true;
			if ( wids[k] == h_youngsters)isAge = true;
			// grades 4-12
			if ( wids[k] == h_grades   ) isAge = true;
			if ( wids[k] == h_grade    ) isAge = true;
			break;
		}
		if ( isAge ) 
			continue;

		// int16_tcut
		int32_t val;
		// sometimes it can be 2+ words
		int32_t endWord ;
		// "7 days a week" for corralesbosquegallery.com
		datetype_t dt = getDateType ( i, &val , &endWord , wids ,nw,
					      // onPreceeds?
					      savedWid == h_on );
		// the types of dates that are non-numeric
		datetype_t specialTypes =
			DT_HOLIDAY      | // thanksgiving
			DT_SUBDAY       |
			DT_SUBWEEK      | // weekends
			DT_SUBMONTH     | // last day of month
			DT_EVERY_DAY    | // 7 days a week
			DT_SEASON       | // summers
			DT_ALL_HOLIDAYS ; // "holidays"
		// if there, add it
		if ( dt && ( dt & specialTypes ) ) { // DT_HOLIDAY ) {
			// use this now
			Date *DD;
			DD = addDate(dt,defFlags|DF_CANONICAL,
				     //left,right,
				     i,endWord,
				     val );
			if ( ! DD ) return false;
			// fix for "on christmas" (onpreceeds)
			if ( savedWid == h_on )
				DD->m_suppFlags |= SF_ON_PRECEEDS;
			// . this is just for "7 days a week", etc.!!!
			// . so check for "DT_EVERY_DAY"
			// . set SF_HOLIDAY_WORD
			// . i don't like events to start with this base date
			//   when telescoping because it is unclear exactly
			//   what holidays are being referred to!
			//if ( val == HD_HOLIDAYS )
			//	DD->m_suppFlags |= SF_HOLIDAY_WORD;
			// do not re-include words from this type in
			// the next date type!
			i = endWord - 1;
			// that is it
			continue;
		}

		// . get it as time of day, only if hour looks ok
		// . if monthPreceeds is true we got like "Dec 1, 7pm"
		if ( num >= 0 && num <= 24 ) {
			// get its timezone if specified (i.e. "GMT" "MDT",...)
			TimeZone *tz = NULL ;
			int32_t e;
			int32_t tod;
			// if period on left that is bad (javascript?)
			if ( i>0 && wptrs[i][-1]=='.' ) goto notTOD;
			// watch out for dates like 12-15-05 for
			// home.comcast.net
			if ( i+4<nw && 
			     is_digit(wptrs[i+2][0]) &&
			     is_digit(wptrs[i+4][0]) &&
			     words->hasChar(i+1,'-') &&
			     words->hasChar(i+3,'-') ) goto notTOD;
			// . if period on left that is bad (javascript?)
			// . no, hurts "5pm." or "3-5." i would guess.
			//if ( i+1<nw && wptrs[i+1][0]=='.') goto notTOD;
			// single digit + letter is bad if not 'a' or 'p'
			if ( is_alpha_a(wptrs[i][1]) &&
			     to_lower_a(wptrs[i][1]) != 'a' &&
			     to_lower_a(wptrs[i][1]) != 'p' )
				goto notTOD;
			// skip 4/15
			if ( i+1<nw &&
			     ! tids[i+1] && // fix <br/>
			     ! wids[i+1] &&
			     words->hasChar(i+1,'/') )
				goto notTOD;
			// surrounded by non-space punct is bad
			//if ( i>0 && i+1<nw && 
			//     is_punct_a(wptrs[i][-1]) &&
			//     is_punct_a(wptrs[i+1][0]) &&
			//     ! is_wspace_a(wptrs[i][-1]) &&
			//    ! is_wspace_a(wptrs[i+1][0]) )
			//	goto notTOD;
			// set this now too
			bool hadAMPM = false;
			bool hadMinute = false;
			bool isMilitary = false;
			// . get time of day
			// . only should get an isolated tod since we call
			//   addRanges() below to do ranges now
			// . "tod" should be in seconds since midnight
			tod = parseTimeOfDay3(w,i,m_niceness,&e,&tz,saved,
					      &hadAMPM,&hadMinute,
					      &isMilitary);
			// error? g_errno should be set then
			if ( tod == -1 ) {
				if ( ! g_errno ) { char *xx=NULL;*xx=0; }
				return false;
			}
			// add it and continue scan if it was a tod
			if ( tod != -2 ) {
				// use this now
				Date *td =addDate(DT_TOD,defFlags,i,e,tod);
				// g_errno should be set on error
				if ( ! td ) return false;
				// set this flag
				if ( hadAMPM ) 
					td->m_suppFlags |= SF_HAD_AMPM;
				if ( hadMinute )
					td->m_suppFlags |= SF_HAD_MINUTE;
				if ( isMilitary )
					td->m_suppFlags |= SF_MILITARY_TIME;
				// fix for stub hub xml which uses military
				// time but has no am/pm
				//if ( isStubHub )
				//	td->m_suppFlags |= SF_MILITARY_TIME;
				// do not treat 30 in 5:30 as a DAYNUM type
				i = td->m_b - 1;
				// do not go further
				continue;
			}
		}

		// . move it after tod detection!
		// . this hurts "11.25.09" on culturemob.com
		/*
		if ( i+2<nw &&
		     i > 0 &&
		     wptrs[i+1][0]=='.' &&
		     wids[i+2] && 
		     is_digit(wptrs[i+2][0]) &&
		     wptrs[i][-1] !='.' ) {
			// skip that guy 
			i += 2;
			continue;
		}
		*/

 notTOD:
		// get the section hash. hash of all parent tags.
		//int32_t th = sn->m_tagHash;
		// int16_tcut
		int32_t j;
		int32_t minj = i - 20;
		int32_t maxj = i + 20;
		if ( minj <  0 ) minj =  0;
		if ( maxj > nw ) maxj = nw;

		// get number to the left of us
		int32_t leftNum = -1;
		int32_t li      = -1;
		for ( j = i - 1 ; j >= minj ; j-- ) {
			if ( wids[j] == 0LL           ) continue;
			if ( ! is_digit(wptrs[j][0] ) ) break;//continue;
			leftNum = w->getAsLong(j);
			li      = j;
			break;
		}
		// get number to the right of us
		int32_t rightNum = -1;
		int32_t ri       = -1;
		for ( j = i + 1 ; j < maxj ; j++ ) {
			if ( wids[j] == 0LL           ) continue;
			if ( ! is_digit(wptrs[j][0] ) ) break;//continue;
			rightNum = w->getAsLong(j);
			ri       = j;
			break;
		}
		// get number to the right of that!
		int64_t nextWid = 0LL;
		int32_t rightNum2 = -1;
		int32_t ri2       = -1;
		for ( j++ ; j < maxj ; j++ ) {
			if ( wids[j] == 0LL           ) continue;
			if ( ! is_digit(wptrs[j][0] ) ) {
				if ( nextWid == 0LL )
					nextWid = wids[j];
				break;//continue;
			}
			rightNum2 = w->getAsLong(j);
			ri2       = j;
			break;
		}


		// . make sure we have punctuation that can be in numeric dates
		// . set single char to separating punct iff it is one type
		//bool leftPunctOK   = checkPunct ( i-1 );
		char singleChar1 = 0 ;
		char singleChar2 = 0 ;
		char singleChar3 = 0;
		bool rightPunctOK  = checkPunct ( i+1 , w , &singleChar1 );
		bool rightPunctOK2 = checkPunct ( i+3 , w , &singleChar2 );
		checkPunct ( i-1 , w , &singleChar3 );

		//
		// STANDARD TIMESTAMP: 2012-11-19T23:00:00
		// Facebook JSON format uses this.
		// fix trumba.com's "2009-11-23T10:00:00" date.
		if ( ri        == i + 2 &&
		     ri2       == i + 4 &&
		     ri2+3 < nw &&
		     rightPunctOK  &&
		     rightPunctOK2 &&
		     num       >= 1900 && num   <= 2050 &&
		     rightNum  >= 01   && rightNum  <= 12   &&
		     // day must be 1+
		     rightNum2 >= 01   && rightNum2 <= 31   &&
		     // wptrs[ri2]
		     (wptrs[ri2][2]=='T' ||
		      wptrs[ri2][1]=='T' ) &&
		     // no colon before year
		     ((i-1)<= 0 || wptrs[i][-1]!=':') &&
		     // no colon after month
		     wptrs[i+3][0] != ':' &&
		     // no colon after year
		     wptrs[i+1][0] != ':' &&
		     // year must be only digits
		     ! words->hasAlpha(i) &&
		     // no slash before year (embedded url)
		     (i<=0 || wptrs[i][-1]!='/') ) {
			// get it
			int32_t year  = num       ;
			int32_t month = rightNum  ;
			int32_t day   = rightNum2 ;
			char *tpos = &wptrs[ri2][1];
			if ( *tpos != 'T' ) tpos = &wptrs[ri2][2];
			// must be there - sanity check
			if ( *tpos != 'T' ) { char *xx=NULL;*xx=0; }
			// and hour
			int32_t hour = atol(tpos+1);
			// after <day>T<hour>:<minute>:<second>
			int32_t endi = ri2 + 5; // ri3
			// minute pos
			char *mpos = tpos + 2;
			if ( *mpos != ':' ) mpos++;
			int32_t minute = 0;
			if ( *mpos == ':' ) minute = atol(mpos+1);
			// make a tod
			int32_t tod = hour * 3600 + minute * 60;
			// tmp flag
			dateflags_t df2 = defFlags;//DF_FROM_BODY;
			// let's say neigher because it is messing up
			// our m_dateFormat deterimination below! were we
			// set m_dateFormat based on # of american vs. 
			// european dates!
			// flag this too
			df2 |= DF_MONTH_NUMERIC;
			// in a style, script, select, marquee tag?
			//if ( flags & badFlags ) df2 |= DF_INBADTAG;
			// make 3 simple dates
			int32_t ni = m_numDatePtrs;
			int32_t nj = m_numDatePtrs+1;
			int32_t nk = m_numDatePtrs+2;
			int32_t nx = m_numDatePtrs+3;
			Date *di = addDate (DT_DAYNUM ,df2,ri2,ri2+1,day);
			if ( ! di ) return false;
			Date *dj = addDate (DT_MONTH  ,df2,ri,ri+1,month);
			if ( ! dj ) return false;
			Date *dk = addDate (DT_YEAR   ,df2,i,i+1,year);
			if ( ! dk ) return false;
			Date *dx = addDate (DT_TOD   ,df2,ri2,endi,tod);
			if ( ! dx ) return false;
			// tag before us?
			if ( ! isTrumba ) df2 |= DF_OFFICIAL;
			// make a compound date
			Date *DD = addDate ( DT_COMPOUND,df2,i,endi,0);
			if ( ! DD ) return false;
			// and add those 3 simple dates to "DD"
			DD->addPtr ( di , ni , this );
			DD->addPtr ( dj , nj , this );
			DD->addPtr ( dk , nk , this );
			DD->addPtr ( dx , nx , this );
			// . ignore this date for trumba. HACK!
			// . and the stubHub xml feed we get now should
			//   ignore them as well, and juse use
			//   <str name="event_date_local"> and
			//   <str name="event_time_local">
			if ( isTrumba ) { // || isStubHub ) {
				// set it for all otherwise Sections.cpp
				// will set SEC_HAS_MONTH because the
				// individual month, "dj" is not ignored!
				di->m_flags5 |= DF5_IGNORE;
				dj->m_flags5 |= DF5_IGNORE;
				dk->m_flags5 |= DF5_IGNORE;
				dx->m_flags5 |= DF5_IGNORE;
				DD->m_flags5 |= DF5_IGNORE;
			}
			// use this now
			//addDate ( DT_MONTH|DT_DAYNUM|DT_YEAR,
			//	  df2,i-2,i+3,month,day,year);
			// skip the stuff
			i = ri2;
			// do not go further
			continue;
		}


		// MONTH<PUNCT>DAY<PUNCT>YEAR (american)
		// DAY<PUNCT>MONTH<PUNCT>YEAR (european)
		if ( num >= 1 && num <= 31 && // 12 &&
		     // one must be <= 12
		     (num <= 12 || rightNum <= 12 ) &&
		     rightPunctOK &&
		     rightPunctOK2 &&
		     ri        == i + 2 &&
		     ri2       == i + 4 &&
		     rightNum  >= 1    && rightNum  <= 31   &&
		     ( (rightNum2 >= 00 && rightNum2 <= 99)||
		       (rightNum2 >= 1900 && rightNum2 <= 2050) ) &&
		     // year must have 2 chars in it at least
		     wlens[ri2] >= 2 &&
		     // year must be only digits
		     ! words->hasAlpha(ri2) &&
		     // no colon before month
		     ((i-1)<= 0 || wptrs[i][-1]!=':' ) &&
		     // no colon after month
		     wptrs[i+1][0]!=':' &&
		     // no colon after day
		     wptrs[ri+1][0]!=':' &&
		     // no slash before month (embedded url)
		     ( i<=0 || wptrs[ i][-1]!='/') &&
		     // only digits in the month, stop "11th" or "11am"
		     ! words->hasAlpha(i) &&
		     // no :<digit> afer year
		    (ri2+1>=nw||
		     wptrs[ri2+1][0]!=':'||
		     !is_digit(wptrs[ri2+1][1]))){

			// if a ':'
			// get it
			//int32_t month = num;
			//int32_t day   = rightNum ;
			int32_t year  = rightNum2;
			// adjust year if it was only two digits
			if ( year <= 99 ) {
				// otherwise, it is probably a year ending
				if ( year > 50 ) year += 1900;
				else             year += 2000;
			}
			// american format
			if ( num <= 12 ) {
				dateflags_t df2 = defFlags | DF_MONTH_NUMERIC;
				df2 |= DF_AMERICAN;
				if ( rightNum <= 12 ) df2 |= DF_AMBIGUOUS;
				// make 3 simple dates
				int32_t ni = m_numDatePtrs;
				int32_t nj = m_numDatePtrs+1;
				int32_t nk = m_numDatePtrs+2;
				Date *di ;
				di = addDate (DT_DAYNUM,df2,ri,ri+1,rightNum);
				if ( ! di ) return false;
				Date *dj = addDate (DT_MONTH,df2,i,i+1,num);
				if ( ! dj ) return false;
				Date *dk = addDate(DT_YEAR,df2,ri2,ri2+1,year);
				if ( ! dk ) return false;
				// make a compound date
				Date *DD = addDate (DT_COMPOUND,df2,i,ri2+1,0);
				if ( ! DD ) return false;
				// and add those 3 simple dates to "DD"
				DD->addPtr ( di , ni , this );
				DD->addPtr ( dj , nj , this );
				DD->addPtr ( dk , nk , this );
			}
			// european format
			if ( rightNum <= 12 ) {
				dateflags_t df2 = defFlags | DF_MONTH_NUMERIC;
				df2 |= DF_EUROPEAN;
				if ( num <= 12 ) df2 |= DF_AMBIGUOUS;
				// make 3 simple dates
				int32_t ni = m_numDatePtrs;
				int32_t nj = m_numDatePtrs+1;
				int32_t nk = m_numDatePtrs+2;
				Date *di = addDate(DT_DAYNUM,df2,i,i+1,num);
				if ( ! di ) return false;
				Date *dj;
				dj = addDate(DT_MONTH,df2,ri,ri+1,rightNum);
				if ( ! dj ) return false;
				Date *dk = addDate(DT_YEAR,df2,ri2,ri2+1,year);
				if ( ! dk ) return false;
				// make a compound date
				Date *DD = addDate (DT_COMPOUND,df2,i,ri2+1,0);
				if ( ! DD ) return false;
				// and add those 3 simple dates to "DD"
				DD->addPtr ( di , ni , this );
				DD->addPtr ( dj , nj , this );
				DD->addPtr ( dk , nk , this );
			}
			// skip over year
			i = ri2;
			// do not go further
			continue;
		}

		// and eweek does this! 2004-07-19
		// YEAR<PUNCT>MONTH<PUNCT>DAY
		if ( ri        == i + 2 &&
		     ri2       == i + 4 &&
		     rightPunctOK  &&
		     rightPunctOK2 &&
		     num       >= 1900 && num   <= 2050 &&
		     rightNum  >= 01   && rightNum  <= 12   &&
		     // day must be 1+
		     rightNum2 >= 01   && rightNum2 <= 31   &&
		     // no colon before year
		     ((i-1)<= 0 || wptrs[i][-1]!=':') &&
		     // no colon after month
		     wptrs[i+3][0] != ':' &&
		     // no colon after year
		     wptrs[i+1][0] != ':' &&
		     // year must be only digits
		     ! words->hasAlpha(i) &&
		     // no slash before year (embedded url)
		     (i<=0 || wptrs[i][-1]!='/') &&
		     // no :<digit> afer day
		     // THIS HURTS 2012-11-19T23:00:00
		     (ri2+1>=nw ||
		      wptrs[ri2+1][0]!=':'||
		      !is_digit(wptrs[ri2+1][1])) &&
		     // fix "2011 - 9pm-3am"
		     words->isNum(ri ) &&
		     words->isNum(ri2) ) {
		     // fix trumba.com's "2009-11-23T10:00:00" date
		      //!words->isNum(ri))) {
			// get it
			int32_t year  = num       ;
			int32_t month = rightNum  ;
			int32_t day   = rightNum2 ;
			// tmp flag
			dateflags_t df2 = defFlags;//DF_FROM_BODY;
			// let's say neigher because it is messing up
			// our m_dateFormat deterimination below! were we
			// set m_dateFormat based on # of american vs. 
			// european dates!
			// flag this too
			df2 |= DF_MONTH_NUMERIC;
			// in a style, script, select, marquee tag?
			//if ( flags & badFlags ) df2 |= DF_INBADTAG;

			// make 3 simple dates
			int32_t ni = m_numDatePtrs;
			int32_t nj = m_numDatePtrs+1;
			int32_t nk = m_numDatePtrs+2;
			Date *di = addDate (DT_DAYNUM ,df2,ri2,ri2+1,day);
			if ( ! di ) return false;
			Date *dj = addDate (DT_MONTH  ,df2,ri,ri+1,month);
			if ( ! dj ) return false;
			Date *dk = addDate (DT_YEAR   ,df2,i,i+1,year);
			if ( ! dk ) return false;
			// make a compound date
			Date *DD = addDate ( DT_COMPOUND,df2,i,ri2+1,0);
			if ( ! DD ) return false;
			// and add those 3 simple dates to "DD"
			DD->addPtr ( di , ni , this );
			DD->addPtr ( dj , nj , this );
			DD->addPtr ( dk , nk , this );
			// use this now
			//addDate ( DT_MONTH|DT_DAYNUM|DT_YEAR,
			//	  df2,i-2,i+3,month,day,year);
			// skip the stuff
			i = ri2;
			// do not go further
			continue;
		}

		// MONTH<PUNCT>YEAR (4/2011)
		if ( num >= 1 && num <= 12 &&
		     ri        == i + 2 &&
		     rightPunctOK &&
		     rightNum  >= 1900    && 
		     rightNum  <= 2050   &&
		     // year must have 4 chars in it
		     wlens[ri] == 4 &&
		     // no colon before month
		     ((i-1)<= 0 || wptrs[i][-1]!=':' ) &&
		     // no colon after month
		     wptrs[i+1][0]!=':' &&
		     // no slash before month (embedded url)
		     ( i<=0 || wptrs[ i][-1]!='/') &&
		     // only digits in the month, stop "11th" or "11am"
		     ! words->hasAlpha(ri) &&
		     // require slash between now to fix
		     // "December, 7 2010" for sfmusictech.com
		     words->hasChar(i+1,'/') &&
		     // no :<digit> afer year
		    (ri+1>=nw||
		     wptrs[ri+1][0]!=':'||
		     !is_digit(wptrs[ri+1][1]))){
			// if a ':'
			// get it
			int32_t month = num;
			int32_t year  = rightNum;
			// tmp flag
			dateflags_t df2 = defFlags;//DF_FROM_BODY;
			// make 2 simple dates
			int32_t nj = m_numDatePtrs;
			int32_t nk = m_numDatePtrs+1;
			Date *dj = addDate (DT_MONTH ,df2,i ,i +1,month);
			if ( ! dj ) return false;
			Date *dk = addDate (DT_YEAR  ,df2,ri,ri+1,year);
			if ( ! dk ) return false;
			// make a compound date
			Date *DD = addDate ( DT_COMPOUND,df2,i,ri+1,0);
			if ( ! DD ) return false;
			// and add those 2 simple dates to "DD"
			DD->addPtr ( dj , nj , this );
			DD->addPtr ( dk , nk , this );
			// skip over year
			i = ri;
			// do not go further
			continue;
		}

		// MONTH/DAY (american - no year)
		// DAY/MONTH (european - no year)
		// TODO:  "1/2" might be fraction!
		if ( num >= 1 && num <= 31 &&
		     ri  == i + 2 &&
		     rightNum >= 1 && rightNum <= 31 &&
		     // slash follows
		     i+3<nw &&
		     wlens[i+1]==1 && 
		     wptrs[i+1][0]=='/' &&
		     wptrs[i+3][0]!='/' &&
		     // can not be zero, whether a month or day
		     num >= 1 &&
		     // MDW: only allow american now to fix "24/7"
		     // to fix guysndollsllc.com page4.html
		     num <= 12 &&
		     // make sure not a year, if any number follows day
		     (rightNum2 <= 1000 || rightNum2 >=2100) &&
		     // at least one of the numbers must be a month!
		     (num <= 12 || rightNum <= 12 ) ) {
			// fix "1/2 mile"
			if ( nextWid == h_miles      ) 
				continue;
			if ( nextWid == h_mile       ) 
				continue;
			if ( nextWid == h_mi         ) 
				continue;
			if ( nextWid == h_kilometers ) 
				continue;
			if ( nextWid == h_km         ) 
				continue;
			// american format (MONTH/DAY)
			if ( num <= 12 ) {
				// make 2 simple dates
				int32_t ni = m_numDatePtrs;
				int32_t nj = m_numDatePtrs+1;
				Date *di;
				Date *dj;
				dateflags_t df2;
				df2 = defFlags | DF_MONTH_NUMERIC |DF_AMERICAN;
				if ( rightNum <= 12 ) df2 |= DF_AMBIGUOUS;
				// first is the month
				di = addDate (DT_MONTH ,df2,i,i+1,num);
				if ( ! di ) return false;
				// then the day
				dj = addDate (DT_DAYNUM,df2,ri,ri+1,rightNum);
				if ( ! dj ) return false;
				// make a compound date
				Date *DD1 = addDate (DT_COMPOUND,df2,i,ri+1,0);
				if ( ! DD1 ) return false;
				// and add those 3 simple dates to "DD"
				DD1->addPtr ( di , ni , this );
				DD1->addPtr ( dj , nj , this );
			}
			// european format (DAY/MONTH)
			if ( rightNum <= 12 ) {
				// make 2 simple dates
				int32_t ni = m_numDatePtrs;
				int32_t nj = m_numDatePtrs+1;
				Date *di;
				Date *dj;
				dateflags_t df2;
				df2 = defFlags | DF_MONTH_NUMERIC |DF_EUROPEAN;
				if ( num <= 12 ) df2 |= DF_AMBIGUOUS;
				// first is the day
				di = addDate (DT_DAYNUM,df2,i,i+1,num);
				if ( ! di ) return false;
				// then the month
				dj = addDate (DT_MONTH ,df2,ri,ri+1,rightNum);
				if ( ! dj ) return false;
				// make a compound date
				Date *DD2 = addDate (DT_COMPOUND,df2,i,ri+1,0);
				if ( ! DD2 ) return false;
				// and add those 3 simple dates to "DD"
				DD2->addPtr ( di , ni , this );
				DD2->addPtr ( dj , nj , this );
			}
			// use this now
			//addDate ( DT_MONTH|DT_DAYNUM,df2,i,i+3,month,day,0 );
			// skip daynum! do not add as individual then!
			i = ri;
			// do not go further
			continue;
		}


		// a two digit isolated year?
		if ( num >= 00 && num <= 99 && 
		     // must be exactly two digits
		     wlens[i]==2 &&
		     // require tick, like "'09"
		     i>0 && wptrs[i][-1]=='\'' ) {
			// set value
			int32_t year ;
			if ( num < 20 ) year = 2000 + num;
			else            year = 1900 + num;
			// use this now
			if(!addDate(DT_YEAR,defFlags,i,i+1,year))return false;
			// do not go further
			continue;
		}

		// 2010-11 . year range
		// to fix "October 18, 2011 - 12:13pm" for estarla.com
		// we now look at previous #
		if ( leftNum >= 1900 && 
		     leftNum <= 2050 && 
		     wlens[li]==4 &&
		     m_numDatePtrs > 0 &&
		     m_datePtrs[m_numDatePtrs-1] &&
		     m_datePtrs[m_numDatePtrs-1]->m_type == DT_YEAR &&
		     // fix "karlson - 1952 - 82m" for www.guildcinema.com
		     wlens[i] == 2 &&
		     num <= 99 && 
		     num >= 0 &&
		     num > (leftNum%100) &&
		     singleChar3 == '-' &&
		     // fix trumba's 2009-11-25T02:00:00Z crap
		     singleChar1 != '-' ) {
			// int16_tcut
			dateflags_t df2 = defFlags;//DF_FROM_BODY;
			// the century
			int32_t ccc = (leftNum / 100) * 100;
			int32_t rightYear = num + ccc;
			// sanity check
			if ( num >= rightYear ) { char *xx=NULL;*xx=0; }
			// sanity
			if ( m_numDatePtrs <= 0 ) { char *xx=NULL;*xx=0; }
			// get last date year
			Date *di = m_datePtrs[m_numDatePtrs-1];
			// sanity
			if ( di->m_type != DT_YEAR ) { char *xx=NULL;*xx=0; }
			// use this now
			int32_t ni = m_numDatePtrs-1;
			int32_t nj = m_numDatePtrs;
			//Date *di = addDate (DT_YEAR ,df2,i,i+1,num);
			//if ( ! di ) return false;
			Date *dj = addDate (DT_YEAR ,df2,i,i+1,num+ccc);
			if ( ! dj ) return false;
			// make a compound date
			Date *DD = addDate ( DT_RANGE,df2,li,i+1,0);
			if ( ! DD ) return false;
			// and add those 3 simple dates to "DD"
			DD->addPtr ( di , ni , this );
			DD->addPtr ( dj , nj , this );
			// skip the right num as well!
			//i = ri;
			// do not go further
			continue;
		}

		// . maybe a solo year? 
		if ( num >= 1900 && num <= 2050 && wlens[i]==4 &&
		     // make sure not part of a phone number!
		     // fixes "(815) 942-2032" using 2032 as year for
		     // http://www.drivechicago.com/carshows.aspx
		     (i-2<0 || wlens[i-2]!=3 || !is_digit(wptrs[i-2][0]) ||
		      // fix "November 7th, 2010" for nickhotel.com
		      ! words->isNum(i-2))  ) {
			// use this now
			if (!addDate(DT_YEAR,defFlags,i,i+1,num))return false;
			// do not go further
			continue;
		}

		// "12th" "23rd"
		if ( num >= 1 && num <= 31 && wlens[i]<=4 &&
		     ! is_digit (wptrs[i][wlens[i]-1]) ) {
			// must have right ending
			char *end = wptrs[i] + wlens[i] - 2;
			// do we got it?
			bool good = false;
			// int16_tcut
			char c0 = to_lower_a(end[0]);
			char c1 = to_lower_a(end[1]);
			// check it
			if ( c0 == 's' && c1 == 't' ) good = true;
			if ( c0 == 'n' && c1 == 'd' ) good = true;
			if ( c0 == 'r' && c1 == 'd' ) good = true;
			if ( c0 == 't' && c1 == 'h' ) good = true;
			if ( ! good ) continue;
			// require a month now to preceed to fix
			// "1st and 3rd Saturdays" so we do not label
			// "1st and 3rd" as daynums
			if ( ! saved ) good = false;
			// crap! breaks "27th of November" for
			// nelsoncountylife.com,  so scan forward as well
			// before giving up on it...
			int32_t k = i + 1; 
			int32_t kmax = i + 10;
			if ( kmax > nw ) kmax = nw;
			for ( ; k < kmax ; k++ )
				if ( wids[k] ) break;
			// "of" is ok
			if ( wids[k] == h_of ) good = true;
			// can't have a 6th saturday
			// . crap, makes us lose desc sent "in its 6th year..."
			//   for wonderlandballroom
			//if ( num >= 6 ) good = true;
			// not a daynum if not good
			if ( ! good ) continue;
			// use this now
			if(!addDate(DT_DAYNUM,defFlags,i,i+1,num))return false;
			// keep monthPreceeds set to true
			if ( saved ) monthPreceeds = true;
			// do not go further
			continue;
		}

		// an ISOLATED day of month?
		if ( num >= 1 && num <= 31 && wlens[i]<=2 &&
		     // handle "3C"
		     ( wlens[i]==1 || is_digit(wptrs[i][1]) ) ) {
			// fix for "PG-13" for mrmovies.com url
			if ( i>= 2 &&
			     is_alpha_a(wptrs[i][-2]) &&
			     wptrs[i][-1]=='-' ) 
				continue;
			// . do not allow "(3)" since matches directory pages
			// . "Baked Fresh Daily (1)" on switchboard.com results
			//   in "Daily (1" as date... which sux... and it uses
			//   that as a date header for some store hours there
			if ( i>=1 && 
			     i+1<nw &&
			     ( wptrs[i][-1] == '(' || wptrs[i][-1]=='[' ) &&
			     ( wptrs[i+1][0] == ')'||wptrs[i+1][0]==']') )
				continue;
			// do not allow numbers in urls
			// fixes ceder.net
			// "http://...query&StateId=31 28-September-2009"
			// from pairing up 31 and 28 together into a list
			// . "bb" is NULL if being called from Msg13.cpp
			//   for speed purposes
			if ( bb && (bb[i] & D_IS_IN_URL) ) 
				continue;
			// hyphen after us? "1-2 blocks"  "9-10 nightly"
			bool hyphenAfter = false;
			bool inRange     = false;
			int32_t endPoint    = -1;
			if ( i + 1 < nw && words->hasChar(i+1,'-') )
				hyphenAfter = true;
			// to find alnum word after us, skip possible " - x"
			if ( i + 4 < nw && 
			     hyphenAfter &&
			     words->isNum(i+2) ) {
				inRange = true;
				endPoint = words->getAsLong(i+2);
				// forget it if range is bogus
				//if ( endPoint <= num ) continue; 9-5!!
				if ( endPoint >= 32  ) continue;
			}
			// get previous two alnum words
			int32_t kmin = i - 20;
			if ( kmin < 0 ) kmin = 0;
			int64_t prev1 = 0LL;
			int64_t prev2 = 0LL;
			// these ignore tags
			int64_t PREV1 = 0LL;
			bool hitTag = false;
			for ( int32_t k = i - 1 ; k >= kmin ; k-- ) {
				QUICKPOLL(m_niceness);
				// stop if non-br and breaking tid to fix
				// calendars i guess
				if ( tids[k] &&
				     isBreakingTagId(tids[k]) &&
				     tids[k] != TAG_BR &&
				     tids[k] != TAG_SPAN &&
				     tids[k] != (TAG_SPAN|BACKBIT) ) {
					hitTag = true;
					continue;
				}
				// skip non-words
				if ( ! wids[k] ) continue;
				// skip numbers - prevN can not be numeric
				if ( is_digit(wptrs[k][0]) ) continue;
				// if we hit a tag, we can only do this one
				if ( hitTag ) {
					if ( ! PREV1 ) PREV1 = wids[k];
					break;
				}
				if ( ! prev1 ) { 
					prev1 = wids[k]; 
					PREV1 = wids[k];
					continue; 
				}
				prev2 = wids[k]; 
				break;
			}
			// whose after us
			int32_t kmax = i + 20;
			if ( kmax > nw ) kmax = nw;
			int64_t after1 = 0LL;
			int64_t after2 = 0LL;
			int32_t ai1 = -1;
			for ( int32_t k = i + 1 ; k < kmax ; k++ ) {
				QUICKPOLL(m_niceness);
				// stop if non-br and breaking tid to fix
				// calendars i guess
				if ( tids[k] &&
				     isBreakingTagId(tids[k]) &&
				     // allow br since some calendars
				     // are like <td>3<br> for baldwinemc.com
				     //tids[k] != TAG_BR &&
				     tids[k] != TAG_SPAN &&
				     tids[k] != (TAG_SPAN|BACKBIT) )
					break;
				// skip non words
				if ( ! wids[k] ) continue;
				// skip numbers - afterN can not be numeric
				if ( is_digit(wptrs[k][0]) ) continue;
				if ( ! after1 ) { 
					ai1 = k; 
					after1 = wids[k]; 
					continue; 
				}
				after2 = wids[k]; 
				break;
			}
			// . assume its a good daynum be default
			// . we need to get daynums for calendar sections so
			//   if its a lone number, we assume it is a daynum in
			//   a calendar
			bool good = true;
			// assume daynum?
			datetype_t adt = DT_DAYNUM;
			/*
			// fix "Every Friday (10 weeks)" for dmjuice.com so
			// it does not use 10 as a daynum.
			if ( after1 == h_weeks  ||
			     after1 == h_days   ||
			     after1 == h_months ||
			     after1 == h_years  ||
			     after1 == h_miles  ||
			     // "6-8 Grade Dance"
			     after1 == h_grade  ||
			     after1 == h_mi      )
				good = false;
			// is word after us plural?
			if ( num>1 && 
			     ai1 >= 0 &&
			     to_lower_a(wptrs[ai1][wlens[ai1]-1]) == 's' )
				good = false;
			// 1-10 of 10
			if ( hyphenAfter &&
			     after1 == h_of )
				good = false;
			// results 1 - 10
			if ( prev1 == h_results ||
			     prev1 == h_children ||
			     prev1 == h_seniors ||
			     prev1 == h_ages ||
			     prev1 == h_age ||
			     prev1 == h_kids ||
			     prev1 == h_toddlers ||
			     prev1 == h_youngsters ||
			     prev1 == h_grade ||
			     prev1 == h_grades ||
			     // folkmads.org "a 501(c)(3) organization"
			     prev1 == h_a )
				good = false;
			*/
			// any word really... unless month which we fix below
			if ( after1 || prev1 ) 
				good = false;
			// unless month is before or after
			if ( isMonth ( after1 ) ) good = true;
			if ( isMonth ( prev1  ) ) good = true;
			// if certain words proceed us... good indicator
			// for a TOD! (estarla.com)
			if ( ! good &&
			     hyphenAfter &&
			     // not really required and fixes "from 10 - 3" 
			     // for trumba.com
			     //prev2 == h_runs && 
			     prev1 == h_from ) {
				adt = DT_TOD;
				good = true;
			}
			// 9 - 11 nightly (estarla.com)
			if ( ! good &&
			     hyphenAfter &&
			     after1 == h_nightly ) {
				adt = DT_TOD;
				good = true;
			}
			// . Tues-Fru, 8-5 for collectorsguide.com
			// . "PREV1" is like prev1 but ignores all tags between
			//   it and word #i
			if ( ! good &&
			     ( PREV1 == h_daily ||
			       // Tuesday {evenings|nights|...}
			       PREV1 == h_day || // every day 9-5
			       PREV1 == h_evening ||
			       PREV1 == h_evenings ||
			       PREV1 == h_night ||
			       PREV1 == h_nights ||
			       PREV1 == h_afternoon||
			       PREV1 == h_afternoons||
			       PREV1 == h_sunday ||
			       PREV1 == h_monday ||
			       PREV1 == h_tuesday ||
			       PREV1 == h_wednesday ||
			       PREV1 == h_thursday ||
			       PREV1 == h_friday ||
			       PREV1 == h_saturday ||
			       PREV1 == h_sundays ||
			       PREV1 == h_mondays ||
			       PREV1 == h_tuesdays ||
			       PREV1 == h_wednesdays ||
			       PREV1 == h_thursdays ||
			       PREV1 == h_fridays ||
			       PREV1 == h_saturdays ||
			       PREV1 == h_sun ||
			       PREV1 == h_mon ||
			       PREV1 == h_tues ||
			       PREV1 == h_tue ||
			       PREV1 == h_wed ||
			       PREV1 == h_wednes ||
			       PREV1 == h_thurs ||
			       PREV1 == h_thu ||
			       PREV1 == h_thr ||
			       PREV1 == h_fri ||
			       PREV1 == h_sat ||
			       PREV1 == h_f ) ) {
				adt = DT_TOD;
				good = true;
			}
			
			// forget it if range is bogus. no military for this!
			if ( adt == DT_TOD &&
			     inRange &&
			     endPoint >= 13 )
				continue;
			// if doing a daynum range... x - y, then y > x
			if ( adt == DT_DAYNUM &&
			     inRange &&
			     endPoint <= num )
				continue;
			// being equal is never good for daynums or tods
			if ( inRange && num == endPoint )
				continue;
			// a tod with no range is too fuzzy! we won't know
			// if its am or pm or whatever
			if ( adt == DT_TOD && ! inRange )
				continue;
			// skip if not good
			if ( ! good ) 
				continue;
			// add these as the numbers
			int32_t val1 = num;
			int32_t val2 = endPoint;
			// convert into seconds if a tod
			if ( adt == DT_TOD ) {
				val1 *= 3600;
				val2 *= 3600;
			}
			// . anything from 1 to 5 should be pm i guess
			// . {1,2,3,4,5} - *
			if ( adt == DT_TOD && val1 <= 5*3600 )
				val1 += 12*3600;
			// . make sure endpoing is bigger
			// . so fix 9-5
			if ( adt == DT_TOD && val2 <= val1 )
				val2 += 12*3600;
			// otherwise, if not implied ampm we do not know
			// so it will not get EV_EVENT_CANDIDATE set for it
			// in Events.cpp...

			// . use this now
			// . might be a tod or a daynum
			if(!addDate(adt,defFlags,i,i+1,val1))return false;
			// . keep monthPreceeds set to true so that
			//   "December 10-11, 7:30-9:00pm" does not think that 
			//   "11" is a timeofday
			// . CRAP! but that screws up "April 10 11-1pm" because
			//   it is set when processing "11"!!!
			// . then let's be more careful in parseTimeOfDay3()
			if ( saved && adt == DT_DAYNUM ) monthPreceeds = true;
			// set up for range
			if ( inRange ) {
				// add the 2nd number as same type
				if(!addDate(adt,defFlags,i+2,i+3,val2))
					return false;
				// skip over that date
				i += 2;
			}
			if ( inRange && adt == DT_TOD ) {
				Date *DD = NULL;
				// add the range so we can set SF_IMPLIED_AMPM
				DD = addDate (DT_RANGE_TOD,0,i,i+3,0);
				if ( ! DD ) return false;
				// add the ptrs
				Date *d1 = m_datePtrs[m_numDatePtrs-2];
				Date *d2 = m_datePtrs[m_numDatePtrs-1];
				DD->addPtr ( d1 , m_numDatePtrs - 2, this);
				DD->addPtr ( d2 , m_numDatePtrs - 1, this);
				// set the flags now. this needs to be
				// set so DF_EVENT_CANDIDATE can be set
				// in Events.cpp
				DD->m_suppFlags |= SF_IMPLIED_AMPM;
			}
			// do not go further
			continue;
		}
	}

	///////////////////
	//
	// resolve american/european date formats
	//
	///////////////////
	int32_t american = 0;
	int32_t european = 0;
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get flag
		dateflags_t flags = di->m_flags;
		// skip if not american or european format
		if ( ! ( flags & DF_AMERICAN ) &&
		     ! ( flags & DF_EUROPEAN ) ) 
			continue;
		// skip if ambiguous, does not tell us anything
		if ( flags & DF_AMBIGUOUS ) continue;
		// ok, count the votes
		if ( flags & DF_AMERICAN ) american++;
		if ( flags & DF_EUROPEAN ) european++;
	}
	// do we have a clear indication here - must be a landfall
	m_dateFormat = 0;
	if ( american > 0 && european == 0 ) m_dateFormat = DF_AMERICAN;
	if ( european > 0 && american == 0 ) m_dateFormat = DF_EUROPEAN;
	// if format are not clear, assume american since we are in america
	if ( european == 0 && american == 0 ) m_dateFormat = DF_AMERICAN;
	// int16_tcut
	char *u = ""; if ( m_url ) u = m_url->getUrl();
	// . if we are confused, panic!
	// . XmlDoc::getIndexCode() should abort!
	if ( american > 0 && european > 0 ) {
		log("dates: url %s has ambig ameri/euro date formats.",u);
		//reset();
		m_dateFormatPanic = true;
		//return true;
	}

	// nuke the ambig guys that are the wrong format
	for ( int32_t i = 0 ; i < m_numDatePtrs && m_dateFormat ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get flag
		dateflags_t flags = di->m_flags;
		// must be non-ambig. only numeric dates can be ambiguous!
		if ( ! ( flags & DF_AMBIGUOUS ) ) continue;
		// if we could not be sure, nuke it all!
		if ( m_dateFormatPanic ) {
			m_datePtrs[i] = NULL;
			continue;
		}
		// it is ok now!
		di->m_flags &= ~DF_AMBIGUOUS;
		// unset the one it is not
		if ( m_dateFormat==DF_AMERICAN && (flags & DF_EUROPEAN) )
			m_datePtrs[i] = NULL;
		if ( m_dateFormat==DF_EUROPEAN && (flags & DF_AMERICAN) )
			m_datePtrs[i] = NULL;
	}


	///////////////////////////////////////
	//
	// . part 2 - ADD RANGES
	//
	// . add new Dates that are ranges of two other Date ptrs
	// . Date::m_ptrs[] will contain the two Dates
	//
	// . "5 - 7pm"            (DT_TOD        range)
	// . "oct to nov"         (DT_MONTH      range)
	// . "jan 3 thru 13"      (DT_MONTHDAY  range)
	// . "mon - friday"       (DT_DOW  range)
	// . "1st-5th"            (DT_DAYNUM     range)
	// . "12/5/2009-1/6/2010" (DT_MONTHDAYYEAR range, numeric only here)
	// . "1/15 until 2/30"    (DT_MONTHDAY     range, numeric only here)
	//
	///////////////////////////////////////

	// . this returns false if ran out of pool space (mem)
	// . crap this is getting "November - December 5, 2008" wrong
	// . do not allow open-ended ranges now to fix
	//   "Dec 20th and runs through Feb 7th" because it will just get
	//   "through Feb" as a date at this point
	if ( ! addRanges ( w , false ) ) return false;


	//
	// now convert ranges of daynums into range of TODs if
	// no month preceeds (use m_alnumB == m_alnumA).
	// use the most common time since no am/pm is provided
	// fixes "Daily 9-5" and "Tue-Sun 9-5" for 
	// http://www.collectorsguide.com/ab/abmud.html
	// 
	/*
	Date *last = NULL;
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if nuked from range logic above
		if ( ! di ) continue;
		// save last
		Date *saved = last;
		// update last
		last = di;
		// look for a range of daynums
		if ( di->m_type != DT_RANGE_DAYNUM ) continue;
		// point to the individual daynums
		Date *d1 = di->m_ptrs[0];
		Date *d2 = di->m_ptrs[1];
		// . if beyond 12, not happening
		// . do not support military time for this algo
		if ( d1->m_num >= 13 ) continue;
		if ( d2->m_num >= 13 ) continue;
		// assume not adjacent to pre
		Date *adj = saved;
		// get date right before di
		if ( saved ) { 
			// get alnum words in between it and di, if any
			int32_t k = saved->m_b;
			for ( ; k < di->m_a && ! wids[k] ; k++ ) ;
			// if not adjacent to the previous Date then forget
			// it, leave it as a daynum range
			//if ( k < di->m_a ) continue;
			if ( k < di->m_a ) adj = NULL;
		}
		// if previous date was a month then leave as a daynum range
		if ( adj && adj->m_type == DT_MONTH )
			continue;
		// must be a range
		if ( di->m_type != DT_RANGE_DAYNUM ) { char *xx=NULL;*xx=0; }
		// int16_tcuts
		int32_t num1 = d1->m_num;
		int32_t num2 = d2->m_num;
		// anything from 1 to 5 should be pm i guess
		if ( num1 <= 5    ) { 
			num1 += 12;
			di->m_suppFlags |= SF_IMPLIED_AMPM;
		}
		if ( num2 <= num1 ) {
			num2 += 12;
			di->m_suppFlags |= SF_IMPLIED_AMPM;
		}
		// ignore it if it was like "3-1"
		if ( num1 >= num2 ) continue;
		// otherwise we change the range
		di->m_type    = DT_RANGE_TOD;
		di->m_hasType = DT_RANGE_TOD | DT_TOD;
		// and the individual numbers
		d1->m_hasType = d1->m_type = DT_TOD;
		d2->m_hasType = d2->m_type = DT_TOD;
		// and these flags too!
		d1->m_flags |= DF_EXACT_TOD;
		d2->m_flags |= DF_EXACT_TOD;
		di->m_flags |= DF_EXACT_TOD;
		// adjust
		d1->m_num = num1 * 3600;
		d2->m_num = num2 * 3600;
		// this too!
		d1->m_tod = num1 * 3600;
		d2->m_tod = num2 * 3600;
		d1->m_minTod = d1->m_tod;
		d1->m_maxTod = d1->m_tod;
		d2->m_minTod = d2->m_tod;
		d2->m_maxTod = d2->m_tod;
		// and this
		di->m_minTod = d1->m_tod;
		di->m_maxTod = d2->m_tod;
		di->m_tod    = -2;
	}
	*/

	//
	// before making lists of daynums detect if daynums are in a
	// calendar. we do not even have sections class at this point so
	// we can't use that.
	//
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if nuked from range logic above
		if ( ! di ) continue;
		// must be canonical month
		if ( di->m_type != DT_DAYNUM ) continue;
		// must be the 1st
		if ( di->m_num != 1 ) continue;
		// must be in body
		if ( di->m_a < 0 ) continue;
		// get first word or tag before it
		int32_t k = di->m_a - 1;
		// must have something
		if ( k < 0 ) continue;
		// skip punct crap
		if ( ! wids[k] && ! tids[k] ) k--;
		// must have something
		if ( k < 0 ) continue;
		// not good if not tag
		if ( ! tids[k] ) continue;
		// remember tag id, other daynums must match
		//nodeid_t tagid = tids[k];
		// keep tabs
		int32_t next = 2;
		// assume a cal
		bool gotCal = true;
		// see if we got a complete sequence
		for ( int32_t j = i + 1 ; j < m_numDatePtrs ; j++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// int16_tcut
			Date *dj = m_datePtrs[j];
			// skip if nuked from range logic above
			if ( ! dj ) continue;
			// must be canonical month
			if ( dj->m_type != DT_DAYNUM ) continue;
			// must be in body
			if ( dj->m_a < 0 ) break;
			// get first word or tag before it
			int32_t k = di->m_a - 1;
			// must have something
			if ( k < 0 ) continue;
			// skip punct crap
			if ( ! wids[k] && ! tids[k] ) k--;
			// must have something
			if ( k < 0 ) continue;
			// not good if not tag
			if ( ! tids[k] ) continue;
			// must match, no, some had <a href> links and
			// some did not for carnegieconerts.com
			//if ( tids[k] != tagid ) continue;
			// must be next in line, otherwise not good
			if ( dj->m_num != next++ ) {
				// no way
				gotCal = false;
				break;
			}
			// the 28th? stop then.
			if ( dj->m_num >= 28 ) {
				// point to last one
				di->m_lastDateInCalendar = dj;
				// 28 days in a row is good enough for now
				break;
			}
			// no way
			//gotCal = false;
			//break;
		}
		// skip if no cal
		if ( ! gotCal ) continue;
		// reset this
		next = 1;
		// flag them all!
		for ( int32_t j = i ; j < m_numDatePtrs ; j++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// int16_tcut
			Date *dj = m_datePtrs[j];
			// skip if nuked from range logic above
			if ( ! dj ) continue;
			// must be canonical month
			if ( dj->m_type != DT_DAYNUM ) continue;
			// must be next in line
			if ( dj->m_num != next++ ) break;
			// flag it
			dj->m_flags |= DF_IN_CALENDAR;
		}
	}
	



	///////////////////////////////////////
	//
	// part 3 - ADD LISTS
	//
	// . add new Dates that are lists of other Date ptrs we got already
	// . Date::m_ptrs[] will point to all the Dates
	//
	// . "7pm, 10-11am and 3-4pm"      (DT_TOD       list)
	// . "aug,oct,dec"                 (DT_MONTH     list)
	// . "monday, wednesday thru fri"  (DT_DOW list)
	// . "1,2,3-5, 10"                 (DT_DAYNUM    list)
	//
	///////////////////////////////////////
	if ( ! addLists ( w , 
			  false ) )  // ignore breaking tags?
		return false;


	///////////////////////////////////////
	//
	// . part 4a - SET CANONICAL DATES (we set numeric above)
	//
	// . we set these after adding ranges and lists in order to
	//   detect the examples below
	// . add new Dates that are ranges of two other Date ptrs
	// . Date::m_ptrs[] will contain the two Dates
	//
	// . "nov 11,12-15"       (DT_MONTHDAY , canonical)
	// . "nov 11,12-15 '08"   (DT_MONTHDAYYEAR, canonical)
	//
	///////////////////////////////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if nuked from range logic above
		if ( ! di ) continue;
		// must be canonical month
		if ( di->m_type != DT_MONTH ) continue;
		// set this
		//int32_t month = di->m_num1;
		
		int32_t start = di->m_a;

		int32_t a;
		int32_t b;
		char *p;
		char *pend;

		//
		// init the "j" loop for the day
		//
		int32_t j = i + 1;
		// int16_tcut
		Date *dj = NULL;
		// scan over atomic dates after the month
		for ( ; j < m_numDatePtrs ; j++ ) {
			// set it
			dj = m_datePtrs[j];
			// stop if not null
			if ( dj ) break;
		}
		// nothing
		if ( ! dj ) continue;
		// must be an isolated day (list or range is ok)
		if ( dj->m_type != DT_DAYNUM ) continue;
		// get word range in between
		a = di->m_b;
		b = dj->m_a;
		// must just be one punct word in between
		if ( b - a != 1 ) continue;
		// get that punct word
		p    =     wptrs[a];
		pend = p + wlens[a];
		// must just have punct in between 
		for ( ; p < pend ; p++ ) {
			// space is ok
			if ( is_wspace_utf8 ( p ) ) continue;
			// period is ok only at start (nov. 19)
			if ( *p == '.' && p == wptrs[a] ) continue;
			// nothing else allowed
			break;
		}
		// skip if unacceptable punct in between
		if ( p < pend ) continue;
		// set it
		//int32_t day = dj->m_num1;
		// this too
		int32_t end = dj->m_b;

		/*
		  adding in the year here messes up our ability to get
		  a range for "May 30 through August 22, 2008" for that
		  salsapower.com url, because di->m_hasType != dj->m_hasType
		  in addRanges() because dj->m_hasType included the year!!
		//
		// init the "k" loop for the year
		//
		int32_t k = j + 1;
		// TRY to get year after it
		Date *dk = NULL;
		// scan over atomic dates after the month
		for ( ; k < m_numDatePtrs ; k++ ) {
			// set it
			dk = m_datePtrs[k];
			// stop if not null
			if ( dk ) break;
		}
		// assume no year
		int32_t year = 0;
		// zap it if not year
		if ( dk && dk->m_type != DT_YEAR ) dk = NULL;
		// must be an isolated day (list or range is ok)
		if ( dk ) {
			// get word range in between
			a = dj->m_b;
			b = dk->m_a;
			// must just be one punct word in between
			if ( b - a != 1 ) dk = NULL;
		}
		if ( dk ) {
			// get that punct word
			p    =     wptrs[a];
			pend = p + wlens[a];
			// must just have punct in between 
			for ( ; p < pend && dk ; p++ ) {
				// space is ok
				if ( is_wspace_utf8 ( p ) ) continue;
				// comma is ok (dec 19, 2009)
				if ( *p == ',' ) continue;
				// TODO: allow a tick mark: "nov 19 '09"
				// nothing else allowed
				break;
			}
			// do not use year if unacceptable punct in between 
			// day and yr
			if ( p >= pend ) year = dk->m_num;
			// update end
			end = dk->m_b;
		}
		*/
		// use this now
		Date *DD = addDate ( DT_COMPOUND,DF_CANONICAL,start,end,0);
		if ( ! DD ) return false;
		// and set
		DD->addPtr ( di , i , this );
		DD->addPtr ( dj , j , this );
		// add in the year if we had it
		//if ( dk ) DD->addPtr ( dk , k , this );
	}

	///////////////////////////////////////
	//
	// . part 4b - SET EUROPEAN DAY/CANONICALMONTH DATES
	// 
	// . like above but european format
	// . "01 nov"       (DT_MONTHDAY , canonical)
	//
	///////////////////////////////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if nuked from range logic above
		if ( ! di ) continue;
		// must be canonical month
		if ( di->m_type != DT_MONTH ) continue;
		int32_t a;
		int32_t b;
		char *p;
		char *pend;
		// init the "j" loop for the day
		int32_t j = i - 1;
		// int16_tcut
		Date *dj = NULL;
		// scan over atomic dates before the month
		for ( ; j >= 0 ; j-- ) {
			// set it
			dj = m_datePtrs[j];
			// stop if not null
			if ( dj ) break;
		}
		// nothing
		if ( ! dj ) continue;
		// must be an isolated day (list or range is ok)
		if ( dj->m_type != DT_DAYNUM ) continue;
		// get word range in between
		a = dj->m_b;
		b = di->m_a;

		// if too many words in between, forget it
		if ( b - a > 5 ) continue;

		// . scan words in between
		// . i added this rather than only allowing a single punct
		//   word of predefined characters to make reverbnation.com
		//   make "28 JAN" a compound date even though it was in 
		//   the format "<div>28</div><div>JAN</div>"
		// . BUT the real fix for allowing "8:00 PM [[]] JAN" to
		//   telescope to "28" was fixing the "remove" variable
		//   below so it wouldn't hack off the last part of the
		//   telescope if it was a daynum. so this logic is just here
		//   to protect against something like
		//   "<div>28</div> <div>JAN</div>,<div>15<div> <div>Feb</div>"
		//   i guess
		// . but did put the remove logic back because telescoping
		//   to an individual daynum proved quite spammy and made
		//   a few urls worse...
		int32_t w; for ( w = a+1 ; w < b ; w++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// no other words allowed
			if ( wids[w] ) break;
			// some tags ok
			if ( tids[w] ) {
				nodeid_t tid = tids[w] & BACKBITCOMP;
				if ( tid == TAG_TABLE ) break;
				if ( tid == TAG_FORM  ) break;
				continue;
			}
			// get that punct word
			p    =     wptrs[w];
			pend = p + wlens[w];
			// only one punctuation mark allowed though
			int32_t pcount = 0;
			// must just have punct in between 
			for ( ; p < pend ; p++ ) {
				// space is ok
				if ( is_wspace_utf8 ( p ) ) continue;
				// count it, only one punctuation mark allowed
				if ( ++pcount >= 2 ) break;
				// and hyphen is ok 01-jul-2009
				if ( *p == '-' ) continue;
				// period is ok only like 01.jul.2009
				if ( *p == '.' ) continue;
				// nothing else allowed
				break;
			}
			// stop if unacceptable punct in between
			if ( p < pend ) break;
		}
		// we broke out early if we ran into an obstacle
		if ( w < b ) break;

		// this too
		int32_t start = dj->m_a;
		int32_t end   = di->m_b;
		// use this now
		Date *DD = addDate ( DT_COMPOUND,DF_CANONICAL,start,end,0);
		if ( ! DD ) return false;
		// and set
		DD->addPtr ( di , i , this );
		DD->addPtr ( dj , j , this );
	}

	Date *prev = NULL;
	///////////////////
	//
	// . add supplemental flags that indicate modifiers
	// . update each Date::m_a/m_b if necessary to include modifiers
	//
	///////////////////
	for ( int32_t x = 0 ; x < m_numDatePtrs ; x++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[x];
		// skip if none
		if ( ! di ) continue;

		// save the endpoints, they might be expanded here
		int32_t left  = di->m_a;
		int32_t right = di->m_b;

		// skip if not in the document body (i.e. in url itself)
		if ( left < 0 ) continue;

		// save it
		Date *savedPrev = prev;
		// update it
		prev = di;

		// reset supplemental flags
		suppflags_t sflags = 0;

		datetype_t dt  = di->m_type;
		//int32_t       val = di->m_num;

		// . more special flags
		// . i use these for when have a time like "9:30"
		//   that has no am or pm, and if we have SF_NIGHT
		//   set we can adjust for it in addIntervals()
		int64_t wid = wids[left];
		if ( wid == h_night      ) sflags |= SF_NIGHT;
		if ( wid == h_nights     ) sflags |= SF_NIGHT;
		if ( wid == h_evening    ) sflags |= SF_NIGHT;
		if ( wid == h_evenings   ) sflags |= SF_NIGHT;
		if ( wid == h_afternoon  ) sflags |= SF_AFTERNOON;
		if ( wid == h_afternoons ) sflags |= SF_AFTERNOON;
		if ( wid == h_morning    ) sflags |= SF_MORNING;
		if ( wid == h_mornings   ) sflags |= SF_MORNING;

		// save it
		suppflags_t saved = sflags;

		// check for "every" "first", etc. before our date?
		// [on] 
		// [the|each|every] 
		// [first|second|third|fourth|fifth]
		// **[sun|mon|tues|wed|thu|fri|sat]
		// [of]
		// [the|each|every] 
		// [year|month|week|season|quarter|semester]
		bool modCheck = false;
		if ( dt == DT_DOW ) modCheck = true;
		// fix "EVERY SATURDAY & SUNDAY" for texasdrums
		if ( dt == DT_LIST_DOW ) modCheck = true;
		// mid-august
		if ( dt == DT_MONTH ) modCheck = true;
		// . and allow preceeding words for seasons
		// . fix for "Extended hours Saturdays and Sundays in 
		//   summer: 9am-6pm. " for
		//   http://www.collectorsguide.com/ab/abmud.html
		if ( dt == DT_SEASON ) modCheck = true;
		// . a number like "1st" warrants a mod check
		// . fixes "1st and 15th of every month" for unm.edu url
		// . NO!... hurt "7pm" - so i made this more thorough
		if ( is_digit(wptrs[left][0]) &&
		     wlens[left] >= 3 &&
		     wlens[left] <= 4 &&
		     ((to_lower_a(wptrs[left][wlens[left]-2]) == 's' && // 1st
		       to_lower_a(wptrs[left][wlens[left]-1]) == 't' ) ||

		     ( to_lower_a(wptrs[left][wlens[left]-2]) == 'n' && // 2nd
		       to_lower_a(wptrs[left][wlens[left]-1]) == 'd' ) ||

		     ( to_lower_a(wptrs[left][wlens[left]-2]) == 't' && // 4th
		       to_lower_a(wptrs[left][wlens[left]-1]) == 'h' ) ||

		     ( to_lower_a(wptrs[left][wlens[left]-2]) == 'r' && // 3rd
		       to_lower_a(wptrs[left][wlens[left]-1]) == 'd' ) ))
			modCheck = true;
		// the holiday "holiday" or "holidays" warrant mod check
		// for like "every holiday" or for "non-holiday mondays"
		// as used in collectorsguide.com
		if ( di->m_type == DT_ALL_HOLIDAYS )
			modCheck = true;
		
		// limit range
		int32_t kmin = left - 30;
		if ( kmin < 0  ) kmin = 0;
		int64_t last = 0LL;
		// scan backwards
		for ( int32_t k = left - 1 ; modCheck && k > kmin ; k-- ) {
			// skip non-breaking or br tags
			if ( tids[k] ) {
				// microsoft front page uses
				// <br>s to separate lines
				if(isBreakingTagId(tids[k])&&
				   (tids[k]&BACKBITCOMP)!=
				   TAG_BR)
					break;
				continue;
			}
			// skip punct words
			if ( ! wids[k] ) continue;
			// int16_tcut
			int64_t wid = wids[k];
			// skip transitionals
			if ( wid == h_and    ||
			     wid == h_the    ||
			     wid == h_in     || // in the summer
			     wid == h_during || // during the summer
			     // causes "...in 1912. hours 9am-5pm" for
			     // collectorsguide.com
			     //wid == h_hours  || // hours in the summer
			     wid == h_of    ) {
				left = k;
				continue;
			}

			// . crap. this is huring "the 1st to the 3rd" because
			//   for "3rd", which is type DT_DAYNUM, it is thinking
			//   that "1st" is a modified for it! so only allow
			//   these if di->m_type == DT_DOW i guess 
			//if ( di->m_type != DT_DOW ) break;
			// . or if we hit prev date stop, do not absorb it
			// . i.e. "December 1st" "Monday"
			if ( savedPrev && savedPrev->m_b > k ) break;

			// these set flags
			if      (wid==h_first )	sflags |= SF_FIRST;
			else if (wid==h_1st)	sflags |= SF_FIRST;
			else if (wid==h_last)	sflags |= SF_LAST;
			else if (wid==h_second)	sflags |= SF_SECOND;
			else if (wid==h_2nd)	sflags |= SF_SECOND;
			else if (wid==h_third )	sflags |= SF_THIRD;
			else if (wid==h_3rd ) 	sflags |= SF_THIRD;
			else if (wid==h_fourth) sflags |= SF_FOURTH;
			else if (wid==h_4th) 	sflags |= SF_FOURTH;
			else if (wid==h_fifth ) sflags |= SF_FIFTH;
			else if (wid==h_5th ) 	sflags |= SF_FIFTH;
			else if (wid==h_each  ) sflags |= SF_EVERY;
			else if (wid==h_every ) sflags |= SF_EVERY;
			else if (wid==h_non   ) sflags |= SF_NON;
			else if (wid==h_mid   ) 
				sflags |= SF_MID;
			
			// 1 <sup>st</sup>, 2 nd , etc.
			else if (wid==h_st ||
				 wid==h_nd ||
				 wid==h_rd ||
				 wid==h_th ) {
				last = wid;
				continue;
			}
			else if ( wid == h_1 && last == h_st ) 
				sflags |= SF_FIRST;
			else if ( wid == h_2 && last == h_nd ) 
				sflags |= SF_SECOND;
			else if ( wid == h_3 && last == h_rd ) 
				sflags |= SF_THIRD;
			else if ( wid == h_4 && last == h_th ) 
				sflags |= SF_FOURTH;
			else if ( wid == h_5 && last == h_th ) 
				sflags |= SF_FIFTH;
			
			else 
				break;
			// update it if good though
			left = k;
		}

		// limit range
		int32_t kmax = right + 30;
		if ( kmax > nw ) kmax = nw;
		// include the ith word itself too i guess
		for ( int32_t k = right + 1 ; modCheck && k < kmax ; k++ ) {
			// skip non-=breaking and br tags
			if ( tids[k] ) {
				// microsoft front page uses
				// <br>s to separate lines
				if(isBreakingTagId(tids[k])&&
				   (tids[k]&BACKBITCOMP)!=
				   TAG_BR)
					break;
				// if non breaking ignore it
				// to fix 
				// "Monday </font> Nights"
				continue;
			}
			// skip punct words
			if ( ! wids[k] ) continue;
			// int16_tcut
			int64_t wid = wids[k];
			// skip transitionals
			if ( wid == h_and   ) continue;
			if ( wid == h_the   ) continue;
			if ( wid == h_of    ) continue;
			// second tuesday of the year|month|wk
			if ( wid == h_year  ||
			     wid == h_every ||
			     wid == h_each  ||
			     wid == h_month ||
			     wid == h_hours || // summer hours
			     wid == h_week  ) {
				//fix "1st and 15th of every month" for unm.edu
				sflags |= SF_NON_FUZZY;
				right = k + 1;
				continue;
			}
			break;
		}
		// update
		di->m_a = left;
		di->m_b = right;
		// if we had something, set this
		if ( sflags && sflags != saved ) sflags |= SF_NON_FUZZY;
		// update it
		di->m_suppFlags |= sflags ;


		// is it a recurring DOW? if so, set SF_RECURRING_DOW
		suppflags_t sfmask = 0;
		sfmask |= SF_FIRST;
		sfmask |= SF_LAST;
		sfmask |= SF_SECOND;
		sfmask |= SF_THIRD;
		sfmask |= SF_FOURTH;
		sfmask |= SF_FIFTH;
		sfmask |= SF_EVERY;
		sfmask |= SF_PLURAL; // nights, mornnings...
		if ( di->m_suppFlags & sfmask ) {
			if ( di->m_type == DT_DOW )
				di->m_suppFlags |= SF_RECURRING_DOW;
			// "every saturday & sunday" fix for texasdrums.org
			// neworleans drums.
			else if ( di->m_hasType == (DT_DOW|DT_LIST_DOW) )
				di->m_suppFlags |= SF_RECURRING_DOW;
			// . "nights" in something like "Monday nights"
			// . mornings,evenings
			// . fixes zipscene.com dj johnny b because otherwise
			//   if not a recurring date it telescopes on through
			//   from "10pm[[]]Monday nights" to "jan 10,2011"
			//   but we do not allow that if the dow is recurring
			//   in isCompatible() now. well, we treat a recurring
			//   dow as a daynum in Section::m_dateBits so 
			//   isCompatible() returns false on that.
			else if ( di->m_type == DT_SUBDAY )
				di->m_suppFlags |= SF_RECURRING_DOW;
		}
		// also if the word "services" follows the dow like in
		// "Sunday Services" this will fix abqcsl.org which has
		// "Sunday Services at 9:15 and 11:00 AM" and was getting
		// lost because Events.cpp now screens out such beasties if
		// the SF_RECURRING_DOW flag is not set
		if ( (di->m_type == DT_DOW ||
		      // "tuesday night"?
		      di->m_type == (DT_DOW|DT_COMPOUND|DT_SUBDAY) ) &&
		     di->m_b+1 < nw &&
		     ( strncasecmp("services",wptrs[di->m_b+1],8)==0 ||
		       //strncasecmp("session" ,wptrs[di->m_b+1],7)==0 ||
		       //strncasecmp("milonga" ,wptrs[di->m_b+1],7)==0 ||
		       //strncasecmp("special" ,wptrs[di->m_b+1],7)==0 ||
		       // sunday school
		       strncasecmp("school"  ,wptrs[di->m_b+1],6)==0 ) )
			di->m_suppFlags |= SF_RECURRING_DOW;

		// . or an every day is recurring!
		// . fixes "Sunday at 10:30am [[]] Weekly" for trumba.com
		if ( di->m_type == DT_EVERY_DAY )
			di->m_suppFlags |= SF_RECURRING_DOW;
		// . nothing is recurring if in a classical calendar format!!
		// . fixes weekend warriors for mercury.intouch-usa.com
		if ( di->m_calendarSection )
			di->m_suppFlags &= ~SF_RECURRING_DOW;

		// . in order to do the invalidation algo below
		//   we must have "first" or whatever mods otherwise we
		//   end up making "Every Monday<br>Every Monday at 6pm"
		//   for ceder.net miss that event because 
		//   "Every Monday<br>Every" is a date and gets invalidated
		if ( di->m_type != DT_DAYNUM &&
		     di->m_type != DT_RANGE_DAYNUM &&
		     di->m_type != DT_LIST_DAYNUM ) 
			continue;

		// . invalidate previous dates if they used our modifiers!
		// . fixes "1st and 3rd saturdays" from allowing "1st and 3rd"
		//   to be a list of daynums
		for ( int32_t y = x - 1 ; y >= 0 ; y-- ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// int16_tcut
			Date *dy = m_datePtrs[y];
			// skip if none
			if ( ! dy ) continue;
			// stop if no intersection
			if ( dy->m_b < left ) break;
			// kill it
			dy->m_flags |= DF_INVALID;
			// and force it to be fuzzy so algos below ignore it
			dy->m_flags |= DF_FUZZY;
		}
	}
	


	///////////////////////////////////////
	//
	// . part 5 & 6 - SET RANGES/LISTS OF CANONICAL MONTH/DAY TUPLES
	//
	// . add new Dates that are ranges of two other Date ptrs
	// . Date::m_ptrs[] will contain the two Dates
	//
	// . "nov 11,12-15 '08"      (DT_MONTHDAYYEAR)
	// . "nov 11,12-15"          (DT_MONTHDAY)
	// . "nov 23 - oct 8th 2008"
	//   "dec 1,2-5 and 9th"
	//   "nov 3-6 and 8th"
	// . "nov 23 - oct 8th"
	//
	///////////////////////////////////////

	// make compounds out of month/day pairs first since then
	// call addLists() so it can make sure that something like
	// "Dec 11, 18, Jan 1" is made into a list
	if ( ! makeCompounds ( w     , 
			       true  , // monthDayOnly
			       false , // linkDatesInSameSentence
			       false ) ) // ignoreBreakingTags?
		return false;

	// we gotta recall this, but limit to DT_DAYNUM types this time
	// this returns false if ran out of pool space (mem)
	if ( ! addRanges ( w ) ) return false;

	// will this also fix the veteran's thrift store for unm.edu:
	// "9 am. - 6 pm. Mon. - Sat.\n"
	// "Thur. 9 am. - 7 pm. Sun. 10 am - 4 pm."
	// which was putting "Thur" into a
	//if ( ! makeCompounds ( w , false , false, true ) ) return false;

	// recall lists too
	if ( ! addLists  ( w ,
			   false ) ) // ignore breaking tags?
		return false;

	// . for "ongoing through xyz" (i.e. from now until xyz)
	// . sets DF_ONGOING flag so makeCompounds() will see that and
	//   add the ptrs to the open ended range
	//addOpenEndedRanges ( );

	///////////////////////////////////////
	//
	// . part 7 - MAKE COMPOUND DATES
	//
	// . combine Dates into compounds. add as DF_COMPOUND date type
	//
	// . identify sequences of heterogenous dates
	// . add a compound date for each sequence
	//
	// . "nov 23 to dec 27, monday through friday 4:00, 6:00, 8:00"
	//
	///////////////////////////////////////

	// . first make compounds of just dow/tods or dowRanges/todRanges
	// . so something like "oct 15 - feb 1: m-f 8am-3pm<br>sun 9am-11am"
	//   would not compound up "oct 15 - feb 1: m-f 8am-3pm" and we would
	//   not be able to make a list like "m-f 8am-3pm, sun 9am-11am"
	//   (from unm.edu)
	// . crap we got 
	//  "Friday, November 4, 2011 at 11:00 AM - 
	//   Sunday, November 6, 2011 at 10:00 PM"
	//   and it pairs up the 11:00 AM" with Sunday first here.
	//   how to fix that?
	// . maybe fix by letting the addLists() ignore breaking tags always
	//   since now the items in such lists must be bookended by breaking
	//   tags or other dates. i.e. they have to be isolated.
	// . no, if we do not have this logic here we screw up
	//   "9 am. - 6 pm. Mon. - Sat.<br>Thur 9am - 7pm, Sun 9am-11am..."
	//   for unm.edu family thrift store. 
	// . and we screw up soul power's
	//   "November 23 - 27 on Monday 4:00PM-12:00AM <br>
	//    Tuesday 4:00PM-12:00<br>Wednesday 4:00PM-12:00AM<br>..."
	// . so let's try making make compound not pair across a hyphen then
	//   or "to" or "thru" etc.???
	// . NO! this logic breaks a guy that has "Friday - 12:00pm" or
	//   something and causes the 12:00pm to pair up with the following
	//   date. so bad.
	// . REALLY what we need here is to fix that unm.edu url is to
	//   first make the compounds that produce the most *lists*, so
	//   we'd want to pair up the dowrange/todranges for unm.edu first
	//   since they will then be repeated date types back-to-back
	//if ( ! makeCompounds ( w     ,
	//		       false ,    // monthDayOnly?
	//		       false ,    // linkDatesInSameSentence?
	//		       true  ,    // dowTodOnly?
	//		       false ) )  // ignoreBreakingTags?
	//	return false;

	// then make lists again on those so the above example would be able
	// to produce: "m-f 8am-3pm, sun 9am-11am"
	//if ( ! addLists ( w    ,
	//		  true ) )  // ignoreBreakingTags?
	//	return false;

	// then compound all else regardless of type after we've made the
	// dow/tod lists that have precedence above, even if the elements
	// are separated by breaking tags
	if ( ! makeCompounds ( w     , 
			       false ,   // monthDayOnly?
			       false ,   // linkDatesInSameSentence
			       false ) ) // ignoreBreakingTags?
		return false;

	// lastly, make compounds ignoring breaking tags
	/*
	if ( ! makeCompounds ( w     , 
			       false ,   // monthDayOnly?
			       false ,   // linkDatesInSameSentence
			       true  ) ) // ignoreBreakingTags?
		return false;
	*/

	///////////////////////////////////////
	//
	// . part 5 & 6 - SET RANGES/LISTS OF CANONICAL MONTH/DAY TUPLES
	//
	// . add new Dates that are ranges of two other Date ptrs
	// . Date::m_ptrs[] will contain the two Dates
	//
	// . "nov 11,12-15 '08"      (DT_MONTHDAYYEAR)
	// . "nov 11,12-15"          (DT_MONTHDAY)
	// . "nov 23 - oct 8th 2008"
	//   "dec 1,2-5 and 9th"
	//   "nov 3-6 and 8th"
	//
	///////////////////////////////////////

	// we gotta recall this, but limit to DT_DAYNUM types this time
	if ( ! addRanges ( w ) ) return false;

	// recall lists too
	if ( ! addLists  ( w ,
			   false ) )  // ignore breaking tags?
		return false;

	// ignore breaking tags this time. use them as a precedence operator
	// this one does it
	//if ( ! addLists  ( w , 
	//		   true ) ) // ignore breaking tags?
	//	return false;

	// for "ongoing through xyz" (i.e. from now until xyz)
	//addOpenEndedRanges ( ); this was the one!

	// do this again since we added open ended ranges
	//makeCompounds();

	//
	// set DF_LEFT_BOOKEND and DF_RIGHT_BOOKEND flags
	//
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// multi-word days of weeks (i.e. every wednesday) are
		// automatically bookended
		if ( di->m_type == DT_DOW && (di->m_b-di->m_a>1) ) {
			di->m_flags |= DF_LEFT_BOOKEND;
			di->m_flags |= DF_RIGHT_BOOKEND;
			continue;
		}
		// skip dates taken from in the url
		if ( di->m_a < 0 ) continue;
		// look left
		int32_t a = di->m_a - 1;
		// loop backwards
		for ( ; a >= 0 ; a-- ) {
			// tag is good?
			if ( tids[a] ) {
				di->m_flags |= DF_LEFT_BOOKEND;
				di->m_flags |= DF_HARD_LEFT;
				break;
			}
			// space only punct word is good
			if ( ! wids[a] ) {
				// scan the punct
				char *p    = wptrs[a];
				char *pend = p + wlens[a];
				for ( ; p < pend ; p += getUtf8CharSize(p) ) {
					if ( is_wspace_utf8(p) ) continue;
					// "talk to Dr. May."
					if ( *p == '.' &&
					     a>0 &&
					     wids[a-1] &&
					     isAbbr(wids[a-1]) ) 
						continue;
					// "(between 5th & 6th)"
					if ( *p == '&' ) continue;
					break;
				}
				// do not set flag if nothing definitive found
				if ( p >= pend ) continue;
				// skip if just spaces
				//if ( words->isSpaces(a) ) continue;
				// otherwise, that is good
				di->m_flags |= DF_LEFT_BOOKEND;
				di->m_flags |= DF_HARD_LEFT;
				break;
			}
			// this means do not set flag
			bool ok = false;
			if ( wids[a] == h_at      ) ok = true;
			if ( wids[a] == h_on      ) ok = true;
			if ( wids[a] == h_between ) ok = true;
			if ( wids[a] == h_from    ) ok = true;
			if ( wids[a] == h_and     ) ok = true;
			if ( wids[a] == h_before  ) ok = true;
			if ( wids[a] == h_after   ) ok = true;
			if ( wids[a] == h_the     ) ok = true;
			// "Closed Thanksgiving, Christmas, ..."
			// from collectorsguide.com
			if ( wids[a] == h_closed ) ok = true;
			// case check only applies to alnums
			if ( (di->m_flags & DF_CANONICAL) &&
			     is_upper_utf8(wptrs[di->m_a]) &&
			     is_lower_utf8(wptrs[a]) &&
			     // fix "Colors of Christmas" show for
			     // events.kgoradio.com
			     wids[a] != h_of ) 
				ok = true;
			if ( ok ) di->m_flags |= DF_LEFT_BOOKEND;
			// if we are a single type, we can NOT be bookended
			// with mere words! prevents a lot of ambiguity.
			// fixes  "until midnight [[]] 5 [[]] Sun [[]] Nov..."
			// "here comes the Sun"
			if ( ok && di->m_b - di->m_a > 1 ) 
				di->m_flags |= DF_HARD_LEFT;
			break;
		}
		// and go right
		int32_t b = di->m_b;
		// loop forwards
		for ( ; b < nw ; b++ ) {
			// tag is good?
			if ( tids[b] ) {
				di->m_flags |= DF_RIGHT_BOOKEND;
				di->m_flags |= DF_HARD_RIGHT;
				break;
			}
			// . space only punct word is good
			// . now there is hard and soft punct
			// . periods are generally soft
			if ( ! wids[b] ) {
				// scan the punct
				char *p    = wptrs[b];
				char *pend = p + wlens[b];
				for ( ; p < pend ; p += getUtf8CharSize(p) ) {
					if ( is_wspace_utf8(p) ) continue;
					// "talk to Dr. May."
					if ( *p == '.' &&
					     b-1>0 &&
					     wids[b-1] &&
					     isAbbr(wids[b-1]) ) 
						continue;
					// "(between 5th & 6th)"
					if ( *p == '&' ) continue;
					break;
				}
				// do not set flag if nothing definitive found
				if ( p >= pend ) continue;
				//if ( words->isSpaces(b) ) continue;
				// otherwise, that is good
				di->m_flags |= DF_RIGHT_BOOKEND;
				di->m_flags |= DF_HARD_RIGHT;
				break;
			}
			// this means do not set flag
			bool ok = false;
			if ( wids[b] == h_at      ) ok = true;
			if ( wids[b] == h_on      ) ok = true;
			if ( wids[b] == h_between ) ok = true;
			if ( wids[b] == h_from    ) ok = true;
			if ( wids[b] == h_and     ) ok = true;
			if ( wids[b] == h_before  ) ok = true;
			if ( wids[b] == h_after   ) ok = true;
			if ( wids[b] == h_the     ) ok = true;
			// http://www.salsapower.com/cities/us/newmexico.htm
			// "monday nights" fix
			if ( wids[b] == h_night      ) ok = true;
			if ( wids[b] == h_nights     ) ok = true;
			if ( wids[b] == h_evening    ) ok = true;
			if ( wids[b] == h_evenings   ) ok = true;
			if ( wids[b] == h_morning    ) ok = true;
			if ( wids[b] == h_mornings   ) ok = true;
			if ( wids[b] == h_afternoon  ) ok = true;
			if ( wids[b] == h_afternoons ) ok = true;
			// case check only applies to alnums
			if ( (di->m_flags & DF_CANONICAL) &&
			     is_upper_utf8(wptrs[di->m_a]) &&
			     is_lower_utf8(wptrs[b]) ) 
				ok = true;
			if ( ok ) di->m_flags |= DF_RIGHT_BOOKEND;
			// if we are a single type, we can NOT be bookended
			// with mere words! prevents a lot of ambiguity.
			// fixes  "until midnight [[]] 5 [[]] Sun [[]] Nov..."
			// "here comes the Sun"
			if ( ok && di->m_b - di->m_a > 1 ) 
				di->m_flags |= DF_HARD_RIGHT;
			break;
		}
	}


	// the copyright symbol in utf8 (see Entities.cpp for the code)
	char copy[3];
	copy[0] = 0xc2;
	copy[1] = 0xa9;
	copy[2] = 0x00;
	// scan all years, lists and ranges of years, and look for
	// a preceeding copyright sign. mark such years as DF_COPYRIGHT
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// these can't be headers
		if ( di->m_hasType != DT_YEAR &&
		     di->m_hasType != (DT_YEAR | DT_RANGE) )
			continue;
		// scan backwards for copyright
		bool ignore = false;
		// limit min back
		int32_t min = di->m_a - 20;
		if ( min < 0 ) min = 0;
		// check for copyright word or sign
		for ( int32_t a = di->m_a - 1; a >= min ; a-- ) {
			// skip if tag
			if ( tids[a] ) continue;
			// do we have an alnum word before us here?
			if ( wids[a] ) {
				// if word check for copyright
				if ( wids[a] != h_copyright ) continue;
				ignore = true; 
				break;
			}
			// must have copyright sign in it i guess
			if (! gb_strncasestr(wptrs[a],wlens[a],copy)) continue;
			// ignore the year then
			ignore = true; 
			break;
		}
		// if ok, keep going
		if ( ! ignore ) continue;
		// otherwise, flag it
		di->m_flags |= DF_COPYRIGHT;
	}

	/////////////////////////////
	//
	// set DF_HAS_STRONG_DOW and DF_HAS_WEAK_DOW
	// 
	// . this fixes southgatehouse.com which has a band called 
	//   "Sunday Valley" playing on friday.
	// . so we have to allow a telescope to ignore a DOW if it is
	//   no isolated as "Sunday Valley", part of a phrase
	//
	/////////////////////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// must have a DOW
		if ( ! ( di->m_hasType & DT_DOW ) ) continue;
		// if not a single dow, it is a strong compound,list,range etc.
		if ( di->m_type != DT_DOW ) {
			di->m_flags |= DF_HAS_STRONG_DOW;
			continue;
		}
		// if it has left and right bookend it is strong
		if ( ( di->m_flags & DF_LEFT_BOOKEND ) &&
		     ( di->m_flags & DF_RIGHT_BOOKEND ) ) {
			di->m_flags |= DF_HAS_STRONG_DOW;
			continue;
		}
		// or if modifier, it is strong
		if ( di->m_b - di->m_a >= 2 ) {
			di->m_flags |= DF_HAS_STRONG_DOW;
			continue;
		}
		// otherwise we are weak
		di->m_flags |= DF_HAS_WEAK_DOW;
	}

	// current relative to when it was spidered!
	// this segaults because m_nd is NULL when we call parseDates()
	// in XmlDoc::getExplicitSections
	//int32_t currentYear = getYear ( m_nd->m_spideredTime );
	int32_t now = getTimeGlobal();
	int32_t currentYear = getYear ( now );

	/////////////////////
	//
	// set the fuzzy date flag, DF_FUZZY
	//
	/////////////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// assume it is not
		bool fuzzy = true;

		// same for stubhub kinda
		if ( isStubHub ) {
			// only "<date name="event_date_time_local">
			// 2012-05-04T18:00:00Z</date>" is good
			// get tag before it
			int32_t pre = di->m_a - 1;
			if ( pre > 0 &&
			     wlens[pre] == 35 &&
			     !strncmp(wptrs[pre],"<date name=\"event_date_"
				      "time_local\">",35) )
				continue;
			// otherwise, ignore it!
			di->m_flags |= DF_FUZZY;
			di->m_flags5 |= DF5_IGNORE;
			continue;
		}

		// int16_tcut setups for eventbrite
		Section *sk = NULL;
		if ( di->m_a >= 0 ) sk = m_sections->m_sectionPtrs[di->m_a];
		// check out the parent section
		Section *sp = NULL; 
		if ( sk ) sp = sk->m_parent;
		// and parent of that
		Section *spp = NULL;
		if ( sp ) spp = sp->m_parent;

		// eventbrite. ignore all dates too basically
		if ( isEventBrite ) {
			// only "<event><start_date>...</>" is good
			if ( sp &&
			     ! strncmp(m_wptrs[sp->m_a],"<start_date>",12) &&
			     // if no parent or its <event> then we are the
			     // start_date of the event and not in a 
			     // <ticket> section!
			     ( ! spp || 
			       ! spp->m_tagId ||
			       ! strncmp(m_wptrs[spp->m_a],"<event>",7) ) )
				continue;
			// otherwise, ignore it!
			di->m_flags |= DF_FUZZY;
			di->m_flags5 |= DF5_IGNORE;
			continue;
		}

		// stupid facebook json format no longer has plain
		// unix timestamps, it has trumba style timestamps.
		// by definition official times are non-fuzzy
		if ( di->m_flags & DF_OFFICIAL ) continue;

		// all dates in facebook xml are fuzzy except the timestamps
		if ( isFacebook ) {
			// timstamps are the only ones!
			if ( di->m_hasType & DT_TIMESTAMP ) continue;
			// skip all else
			di->m_flags |= DF_FUZZY;
			continue;
		}

		// "in Sunday's Albuquerque Journal" fix for villr.com
		// was causing "in Sunday" to telescope to tod hours
		if ( di->m_type == DT_DOW && di->m_a >= 0 ) {
			int64_t wid = wids[di->m_a];
			if ( wid == h_in ) {
				di->m_flags |= DF_FUZZY;
				continue;
			}
		}
		// get alnum word before date
		int64_t prevWid = 0;
		int32_t jmin = di->m_a - 20;
		if ( jmin < 0 ) jmin = 0;
		for ( int32_t j = di->m_a - 1 ; j >= jmin ; j-- ) {
			// breathe
			QUICKPOLL(m_niceness);
			if ( ! wids[j] ) continue;
			prevWid = wids[j];
			break;
		}
		// fix "2-5 pm [[]] before 4th Saturday" for
		// folkmads.org which had
		// "... dance and singing before 4th Saturday contra dance.".
		// in general, though, i don't think telescoping a tod or tod 
		// range to an ongoing dow range is going to be stable
		if ( di->m_type == DT_DOW && (di->m_flags & DF_ONGOING) ) {
			di->m_flags |= DF_FUZZY;
			continue;
		}

		// fix "*and Sun*-dried tomatoes"
		if ( di->m_a >= 0 &&
		     wids[di->m_a] == h_and &&
		     wids[di->m_b-1] == h_sun ) {
			//di->m_flags |= DF_HAS_WEAK_DOW;
			//di->m_flags &= ~DF_LEFT_BOOKEND;
			//di->m_flags &= ~DF_RIGHT_BOOKEND;
			di->m_flags |= DF_FUZZY;
			continue;
		}

		// next/this sunday (next/this december?)
		if ( (prevWid == h_next ||  prevWid == h_this) &&
		     (di->m_type == DT_DOW || di->m_type == DT_MONTH ) &&
		     // make sure its not a numeric month!!
		     ! is_digit(wptrs[di->m_a][0]) )
			fuzzy = false;

		// ranges and compounds can not be fuzzy, unless daynum
		// only like "1 - 3"
		if ( ( di->m_hasType & DT_RANGE_ANY ) &&
		     // exception: range of daynums can be fuzzy!
		     di->m_type != DT_RANGE_DAYNUM )
			fuzzy = false;
		// compounds cannot be fuzzy, unless its like "1/3"
		if ( ( di->m_hasType & DT_COMPOUND ) &&
		     ( di->m_hasType != (DT_DAYNUM|DT_MONTH|DT_COMPOUND) ||
		       ! ( di->m_flags & DF_MONTH_NUMERIC ) ) )
			fuzzy = false;
		// these can't be headers
		//if ( di->m_type == DT_MOD ) continue;
		// assum header
		//di->m_flags |= DF_HEADER;
		// has soft bookends? (is it capitalized and not surroundings?)
		bool softEnds = ( ( di->m_flags & DF_LEFT_BOOKEND  ) &&
				  ( di->m_flags & DF_RIGHT_BOOKEND ) );
		// must be bookended on both ends to be a header
		if ( softEnds ) 
			fuzzy = false;
		// . but if a weak type, negate that
		// . especially problematic headers because of false matches
		//   like "Joe Friday" or "Your First Practice"
		// . but Joe Friday should not have DF_LEFT_BOOKEND set
		// . and this was causing us to miss some important headers
		//   for the salsa in new mexico url which included dows like
		//   "Tuesdays with Darrin Visarraga ... 7 - 8:30 p.m." and
		//   "Free Salsa Classes on Monday nights ... 9:30  10:30"
		//if ( di->m_type == DT_DOW    ) fuzzy = true;
		if ( di->m_type == DT_DAYNUM ) fuzzy = true;
		// or "A Tuna Christmas" (name of play)
		if ( di->m_type == DT_HOLIDAY &&
		     di->m_num != HD_NEW_YEARS_EVE &&
		     di->m_num != HD_NEW_YEARS_DAY &&
		     // if we are a holiday adjacent to another holiday then we
		     // are not fuzzy... the problem is is that we do not make
		     // lists out of holidays because they may all telescope to
		     // a different DOW/TOD pair, like how collectorsguide.com
		     // has weekday hours and weekend hours. the holiday may 
		     // fall on different days of the week, so we can't really
		     // clump them together
		     ! softEnds )
			fuzzy = true;
		// might have "1/3 pound" or some fraction that we take
		// for a month/day, so do not allow that to be a header
		// unless it is double bookended of courses
		if ( di->m_hasType == (DT_MONTH|DT_DAYNUM) && 
		     ! (di->m_flags & DF_CANONICAL )   ) 
			fuzzy = true;
		// but hard bookends trump everything
		if ( (di->m_flags & DF_HARD_LEFT ) &&
		     (di->m_flags & DF_HARD_RIGHT)   ) fuzzy = false;
		// . BUT do ALLOW "every wednesday" and other DT_DOW types that
		//   have two or more words
		// fixes "every Wednesday from 3:00pm to 4:00 pm, starting 
		//        September 9 and continuing through December 9."
		//        so that "every Wednesday" is telescoped to
		if ( (di->m_hasType & DT_DOW) &&(di->m_b-di->m_a)>1) 
			fuzzy = false;
		// fix for "Midnight" for http://www.guildcinema.com/
		bool isMidnight = false;
		if ( di->m_a >= 0 && wids[di->m_a] == h_midnight ) 
			isMidnight = true;
		if ( di->m_a >= 0 && wids[di->m_a] == h_midday   ) 
			isMidnight = true;
		if ( di->m_a >= 0 && wids[di->m_a] == h_noon     ) 
			isMidnight = true;
		if ( di->m_a >= 0 && wids[di->m_a] == h_dawn     ) 
			isMidnight = true;
		if ( di->m_a >= 0 && wids[di->m_a] == h_sunrise     ) 
			isMidnight = true;
		if ( di->m_a >= 0 && wids[di->m_a] == h_dusk     ) 
			isMidnight = true;
		if ( di->m_a >= 0 && wids[di->m_a] == h_sunset     ) 
			isMidnight = true;
		if ( di->m_a >= 0 && wids[di->m_a] == h_sundown )
			isMidnight = true;
		if ( di->m_b != di->m_a + 1      ) isMidnight = false;
		// time of days are never fuzzy cuz they have am/pm or :'s
		// we had an event that had multiple tods and one dow like:
		// "Monday nights 7:00 intermediate tango; 7:45 beginnering 
		// tango; 8:30-10:15 open dancing. At: Lloyd Shaw, 5506 Coal",
		// and the first two tods were fuzzy, and we missed them!
		if ( (di->m_hasType & DT_TOD) && ! isMidnight )
			fuzzy = false;
		// this is not fuzzy
		if ( di->m_type ==DT_SUBMONTH && di->m_num==HD_MONTH_LAST_DAY)
			fuzzy = false;
		if ( di->m_type ==DT_SUBMONTH && di->m_num==HD_MONTH_FIRST_DAY)
			fuzzy = false;
		// holidays are not fuzzy i guess
		if ( di->m_hasType == DT_HOLIDAY      ||
		     di->m_hasType == DT_SUBDAY       ||
		     di->m_hasType == DT_SUBWEEK      ||
		     di->m_hasType == DT_SUBMONTH     ||
		     di->m_hasType == DT_EVERY_DAY    ||
		     di->m_hasType == DT_SEASON       ||
		     di->m_hasType == DT_ALL_HOLIDAYS ) {
			// . unless a season with some supplemental flags or
			//   something like "every christmas"
			// . CAREFUL about "Night", that has SF_NIGHT set
			if ( di->m_suppFlags & SF_EVERY ) fuzzy = false;
			
			// . closed holiday dates are not fuzzy
			// . this is not set here so don't worry about it here
			//   but when we set DF_CLOSE_DATE in setPart2() then
			//   unset DF_FUZZY there (mdw oct 6, 2010)
			//if ( di->m_flags & DF_CLOSE_DATE ) fuzzy = false;

			// . if a non-season "holiday", we are not fuzzy
			// . no because we need to stop things like
			//   "Christmas Party" from being "Christmas" day...
			//if ( di->m_num != HD_SUMMER &&
			//     di->m_num != HD_FALL &&
			//     di->m_num != HD_WINTER &&
			//     di->m_num != HD_SPRING )
			//	continue;
		}
		// not fuzzy if this is set
		if ( di->m_suppFlags & SF_NON_FUZZY )
			fuzzy = false;
		// a list of months is not fuzzy. fixes "Jan. & Sept" so it
		// can be a close date header for collectorsguide.com
		if ( di->m_type == DT_LIST_MONTH )
			fuzzy = false;
		// . this is always fuzzy: (5th & 6th)
		// . fix for blackbirdbuvette that has (between 5th & 6th)
		//   in its street address
		if ( di->m_type == DT_LIST_DAYNUM )
			fuzzy = true;
		// fix for "Sunday Services" dow for abqcsl.org so the
		// times "9:15 and 11:00 AM" pair up with "Sunday" in
		// "Sunday Services"
		// BUT this hurts "Sunday Valley" band name for 
		// soutgatehouse.com which makes us think Sunday [[]] 9pm is
		// the date of the event, so let's look for certain words
		// after the day of week, like "Services" or "Brunch" or
		// some other event-y word, although couldn't a band be named
		// "Sunday Brunch"?
		// yeah, i had to do this because themixtress.com had:
		// "Eddy Shades Media Presents: Blame It On Wednesdays 
		//  Afterwork at Roam Lounge" ... but was playing on june 23rd
		// 2010, which is a wednesday... hmmm. but not every wednesday
		if ( di->m_type == DT_DOW && 
		     (di->m_flags & DF_LEFT_BOOKEND) ) {
			// debug for now
			//fuzzy = false;
			// get word after it
			int32_t j = di->m_a+1;
			int32_t jmax = j + 20;
			if ( jmax > nw ) jmax = nw;
			int64_t nextWid = 0LL;
			for ( ; j < jmax ; j++ ) {
				QUICKPOLL(m_niceness);
				if ( ! wids[j] ) continue;
				nextWid = wids[j];
				break;
			}
			if ( nextWid == h_services ) fuzzy = false;
			if ( nextWid == h_service  ) fuzzy = false;
			// "SUNDAY SHOW" "SUNDAY RODEO" ...
			// "show" "rodeo" "showcase" "class" ... big list
			//if ( isEventEnding ( &nextWid ) ) fuzzy = false;
			int32_t a = j;
			int32_t b = j+1;
			if ( b > nw ) b = nw;
			if ( a > nw ) a = nw;
			if (hasTitleWords(0,a,b,1,bits,words,false,niceness))
				fuzzy = false;
		}

		// if it is a daynum.. fix for "(5-year increments)"
		if ( di->m_type == DT_DAYNUM && 
		     di->m_a+1 < nw &&
		     // tag must not immediately follow for this
		     !tids[di->m_a+1] ) {
			// scan right of it
			char *p    =     wptrs[di->m_a+1];
			char *pend = p + wlens[di->m_a+1];
			// scan
			for ( ; p < pend ; p++ ) {
				// any punct is generally bad
				if ( is_wspace_a(*p) ) continue;
				// ok, make it fuzzy
				fuzzy = true;
				break;
			}
		}
		// . lowercase "may" is fuzzy
		// . will this fix gmsstrings.com/default.aspx ???
		if ( di->m_type == DT_MONTH &&
		     di->m_month == 5 && // "may"
		     ! di->m_suppFlags && 
		     wptrs[di->m_a][0]=='m' )
			fuzzy = true;
		// . "mid-november" by itself is fuzzy, unless in a range!
		// . like villr.com has it in a range
		// . but estarla.com does not have mid-August in a range
		if ( di->m_type == DT_MONTH &&
		     (di->m_suppFlags & SF_MID) )
			fuzzy = true;

		// get sentence containing
		Section *ss = NULL;
		// . Msg13 hasGoodDate() uses NULL for sections
		// . if its javascript, numSections is 0! check for that
		if ( di->m_a > 0 && sections && sections->m_numSections > 0 ) {
			ss = sections->m_sectionPtrs[di->m_a];
			ss = ss->m_sentenceSection;
		}
		// not in sentence? i guess in url. then not fuzzy!
		if ( di->m_hasType == DT_YEAR && ! ss ) 
			fuzzy = false;
		// a single year in a sentence...?
		// "Summerfest 2011"
		// "2011 Dance in the Park"
		// "The pub was founded in 208."
		// "Doing business since 2009."
		if ( di->m_hasType == DT_YEAR && ss ) {
			// assume not fuzzy
			fuzzy = false;
			// "since 2007" is a fuzzy year
			if ( prevWid == h_since ) fuzzy = true;
			// established/founded in 2003
			//if(prevWid == h_in && prevWid2IsLower ) fuzzy = true;
			// or founded in ...
			for ( int32_t i = ss->m_a ; i < ss->m_b ; i++ ) {
				// skip if not alnum word
				if ( ! wids[i] ) continue;
				if ( wids[i] == h_founded ) fuzzy = true;
				if ( wids[i] == h_established ) fuzzy = true;
				// a mixed case algo

			}
			// these flags are now set in setSentFlagsPart1()
			// so WE can access them. that is why i have Part1
			// and Part2 now... just for this...
			if ( (ss->m_sentFlags & SENT_PERIOD_ENDS) &&
			     (ss->m_sentFlags & SENT_MIXED_CASE ))
				fuzzy = true;
			// get word after date, if any. see if part of
			// a street address like 2001 Main Street. these
			// are only verified or inlined streets methinks...
			int32_t a2 = di->m_a + 2;
			if ( a2<nw && (bb[a2] & D_IS_IN_ADDRESS) )
				fuzzy = true;
			// see if a street indicator follows the year in
			// the same sentence
			for ( int32_t i = di->m_a + 2 ; i < ss->m_b ; i++ ) {
				// skip if not alnum word
				if ( ! wids[i] ) continue;
				// get indicator class
				IndDesc *id;
				id =(IndDesc *)g_indicators.getValue(&wids[i]);
				// . see if like "street" or "northwest"
				// . 2001 18th st
				// . 2000 Mountain NW
				// . TODO: what about northwest conference 2011
				if ( id&&(id->m_bit & (IND_STREET|IND_DIR)) ) 
					fuzzy = true;
			}
			// web wasn't really arround in 1993 so it must be
			// talking about something historical and not related
			// to the event time
			if ( di->m_num <= 1993 )
				fuzzy = true;
			if ( di->m_num >= currentYear+4 )
				fuzzy = true;
		}

		// must be fuzzy
		if ( ! fuzzy ) continue;
		// unflag it
		// put this back in a different way!!!!!!!!!!!!!!!!!!!!!
		// mdw left off here
		//di->m_flags &= ~DF_HEADER;
		di->m_flags |= DF_FUZZY;
	}

	////////////////////
	//
	// set D_IS_IN_DATE bit
	//
	// we need this here now since Sections.cpp's METHOD_DOM_PURE needs
	// access to this bit
	//
	//////////////////////

	if ( ! bits ) return true;

	// get bits array
	//wbit_t *bb = bits->m_bits;

	// now scan the dates we found and set the bits for the words involved
	for ( int32_t i = 0 ; bb && i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get flag
		//dateflags_t flags = di->m_flags;
		// . must be non-ambig. only numeric dates can be ambiguous!
		// . we check the D_IS_IN_DATE flag for event title generation
		//   in Events.cpp, so even if date is ambig, we need to know.
		// . same goes for Address.cpp, we avoid numbers in dates.
		//if ( flags & DF_AMBIGUOUS ) continue;
		// url dates have a and b as -1
		if ( di->m_a == -1 || di->m_b == -1 ) continue;
		// skip telescopes
		if ( di->m_type == DT_TELESCOPE ) continue;
		// . ignore isolated daynums/numbers
		// . fixes messed up place names that had single numbers
		//   in them like "Kelly's #1 westside" for 
		//   estrelladelnortevineyard.com
		if ( di->m_type == DT_DAYNUM ) continue;
		// . also fix dexknows.com where a phone number has 2005
		//   in it and Address.cpp thinks it is a year and stop
		//   the scan for the place name
		if ( di->m_type == DT_YEAR ) continue;
		// actually any single date should not be excluded!
		bool forsure = false;
		// . skip if single isolated DOW
		// . fixes "Sun City Plumbing" since "Sun" is no longer
		//   considered DF_FUZZY because it has DF_LEFT_BOOKEND set.
		//   and we did that to fix "Sunday Services" for abqcsl.org
		if ( di->m_type == DT_DOW ) {
			bool fuzzyDOW = true;
			// can't do this because we had "5,000 Fridays"
			// being the title of an event we missed because we
			// thought it was all generic words and dates
			// for when.com
			//if ( di->m_suppFlags ) fuzzyDOW = false;
			// use this mask now
			suppflags_t sfmask = 
				SF_FIRST |
				SF_LAST  |
				SF_SECOND |
				SF_THIRD |
				SF_FOURTH |
				SF_FIFTH |
				SF_EVERY;
			if ( di->m_suppFlags & sfmask )
				fuzzyDOW = false;
			if ( (di->m_flags & DF_LEFT_BOOKEND) &&
			     (di->m_flags & DF_RIGHT_BOOKEND) )
				fuzzyDOW = false;
			// allow fuzzy dows to be part of place names
			if ( fuzzyDOW ) continue;
			// mark it
			forsure = true;
		}
		// . if just a single daynum bail on it
		// . caused us to miss "25 Plum Street"
		//if ( di->m_type == DT_DAYNUM ) continue;
		// an isolated year could be a street address like "1901"
		//if ( di->m_type == DT_YEAR ) continue;
		if ( di->m_hasType & DT_TOD      ) forsure = true;
		if ( di->m_hasType & DT_COMPOUND ) forsure = true;
		if ( di->m_hasType & DT_LIST_ANY ) forsure = true;
		if ( di->m_hasType & DT_RANGE    ) forsure = true;
		// fix title of "Fridays"
		//if ( (di->m_hasType & DT_DOW) &&
		//     (di->m_flags & DF_RIGHT_BOOKEND) ) forsure = true;
		// if we telescoped successfully
		// we do not have telescope data at this point!!
		//if ( di->m_telescope ) forsure = true;
		if ( ! ( di->m_flags & DF_FUZZY )) forsure = true;
		// . always set this one
		// . this is used in Sections::addSentences() to avoid 
		//   splitting a sentence on a colon if a date word is on the
		//   left of the colon. fixes the title generator so we can
		//   keep such dates together in the event titles in
		//   XmlDoc::getEventSummary().
		//wbit_t mask = D_IS_IN_DATE_2;
		if ( ! forsure ) continue;
		//if ( forsure ) mask |= D_IS_IN_DATE;
		// sanity check
		if ( di->m_a < 0 || di->m_a > di->m_b ) { char *xx=NULL;*xx=0;}
		// . set those words bits
		// . assume [a,b) interval (half open)
		for ( int32_t k = di->m_a ; k < di->m_b ; k++ )
			bb[k] |= D_IS_IN_DATE;
	}

	// set D_IS_TOD
	for ( int32_t i = 0 ; i < m_numTotalPtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_totalPtrs[i];
		// must be tod or year i guess
		wbit_t bf = 0;
		if ( di->m_type == DT_MONTH  ) bf = D_IS_MONTH;
		if ( di->m_type == DT_DAYNUM ) bf = D_IS_DAYNUM;
		if ( ! bf ) continue;
		// get this
		int32_t a = di->m_a;
		// skip if in url
		if ( a < 0 ) continue;
		// set those words bits
		for ( int32_t k = di->m_a ; k < di->m_b ; k++ )
			bb[k] |= bf;
	}


	/*

	  this has all kinds of false hits!!!

	////////////////////
	//
	// set SF_DOW_IN_TITLE
	//
	////////////////////
	// 
	// fixes "Tuseday Night Milonga" so we set SF_RECURRING_DOW which
	// means that it happens every tuesday night
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// in body
		if ( di->m_a < 0 ) continue;
		// need dow or dow/subday combo
		if ( di->m_type != DT_DOW &&
		     // tuesday *night*?
		     di->m_type != (DT_DOW|DT_COMPOUND|DT_SUBDAY) )
			continue;
		// skip if already recurring
		if ( di->m_suppFlags & SF_RECURRING_DOW ) 
			continue;
		// all words in date must be capitalized except like
		// apostrophe s
		int32_t j; for ( j = di->m_a ; j < di->m_b ; j++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// skip if not alnum
			if ( ! wids[j] ) continue;
			// this is ok
			if ( wids[j] == h_s ) continue;
			// must be capitalized
			if ( ! is_upper_utf8 ( wptrs[j] ) ) break;
		}
		// if not all upper, bail
		if ( j < di->m_b ) continue;
		// now word before or after must be upper and only
		// separate by spaces
		j = di->m_a - 1;
		int32_t minj = j - 7;
		if ( minj < 0 ) minj = 0;
		for ( ; j > minj ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// watch out for certain tags that break us up
			if ( tids[j] ) {
				// this is not good for us
				if ( (tids[j]&BACKBITCOMP) == TAG_TD ||
				     (tids[j]&BACKBITCOMP) == TAG_TR ||
				     (tids[j]&BACKBITCOMP) == TAG_TABLE )
					break;
				continue;
			}
			if ( ! wids[j] ) {
				// only spaces allowed
				char *p = wptrs[j];
				char *pend = p + wlens[j];
				for ( ; p < pend ; p++ ) {
					QUICKPOLL(m_niceness);
					if ( *p != ' ' ) break;
					continue;
				}
				if ( p < pend ) break;
			}
			// stop if hit another date
			if ( bb[j] & D_IS_IN_DATE ) break;
			// alnum! must be capitalized
			if ( ! is_upper_utf8(wptrs[j]) ) break;
			// ok, we got one!
			di->m_suppFlags |= SF_DOW_IN_TITLE;
			break;
		}
		// if got it, all done
		if ( di->m_suppFlags & SF_DOW_IN_TITLE ) continue;
		// otherwise check to the right of us
		j = di->m_b;
		int32_t maxj = j + 7;
		if ( maxj > nw ) maxj = nw;
		for ( ; j < maxj ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// watch out for certain tags that break us up
			if ( tids[j] ) {
				// this is not good for us
				if ( (tids[j]&BACKBITCOMP) == TAG_TD ||
				     (tids[j]&BACKBITCOMP) == TAG_TR ||
				     (tids[j]&BACKBITCOMP) == TAG_TABLE )
					break;
				continue;
			}
			if ( ! wids[j] ) {
				// only spaces allowed
				char *p = wptrs[j];
				char *pend = p + wlens[j];
				for ( ; p < pend ; p++ ) {
					QUICKPOLL(m_niceness);
					if ( *p != ' ' ) break;
				}
				if ( p < pend ) break;
				continue;
			}
			// stop if hit another date
			if ( bb[j] & D_IS_IN_DATE ) break;
			// alnum! must be capitalized
			if ( ! is_upper_utf8(wptrs[j]) ) break;
			// ok, we got one!
			di->m_suppFlags |= SF_DOW_IN_TITLE;
			// log it
			printDateNeighborhood(di,w);
			break;
		}
	}
	*/

	return true;
}

bool Dates::hasKitchenHours ( Section *si ) {
	if ( ! si ) return false;
	int32_t a = si->m_a;
	int32_t b = si->m_b;
	int64_t prevWid = 0LL;
	//int32_t count = 0;
	// now scan forward from there
	for ( int32_t j = a ; j < b ; j++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip punct words
		if ( ! m_wids[j] ) continue;
		// limit this bad boy in the case of one huge table cell
		//if ( ++count >= 50 ) break;
		// is it register?
		if ( prevWid == h_kitchen && m_wids[j] == h_hours) return true;
		if ( prevWid == h_happy   && m_wids[j] == h_hours) return true;
		if ( prevWid == h_happy   && m_wids[j] == h_hour ) return true;
		prevWid = m_wids[j];
	}
	return false;
}


bool isTicketDate ( int32_t a , int32_t b , int64_t *wids , Bits *bits ,
		    int32_t niceness ) {

	// sanity check
	if ( bits && ! bits->m_inLinkBitsSet ) { char *xx=NULL;*xx=0; }
	// int16_tcut
	wbit_t *bb = NULL; 
	if ( bits ) bb = bits->m_bits;

	int64_t prevWid = 0LL;
	// now scan forward from there
	for ( int32_t j = a ; j < b ; j++ ) {
		// breathe man
		QUICKPOLL(niceness);
		// skip punct words
		if ( ! wids[j] ) continue;
		// skip if in link
		if ( bb && (bb[j] & D_IN_LINK) ) continue;
		// is it register?
		if ( prevWid == h_register                     ) return true;
		if ( prevWid == h_registration                 ) return true;
		if ( prevWid == h_sign && wids[j] == h_up  ) return true;
		if ( prevWid == h_signup                       ) return true;
		if ( prevWid == h_buy && wids[j] == h_tickets ) return true;
		if ( prevWid == h_purchase&&wids[j]==h_tickets) return true;
		if ( prevWid == h_get && wids[j]==h_tickets) return true;
		// "give them tickets to" for santafe playhouse url
		// to cancel out "Max's or Dish n' Spoon" as a place
		// (for Address.cpp's call to isTicketDate())
		if ( prevWid == h_tickets&& wids[j] == h_to )return true;
		//  advance tickets
		if ( prevWid == h_advance&& wids[j] == h_tickets )return true;
		if ( prevWid == h_tickets&& wids[j] == h_to )return true;
		if ( prevWid == h_presale                      ) return true;
		if ( prevWid == h_on && wids[j] == h_sale  ) return true;
		if ( prevWid == h_pre && wids[j] == h_sale ) return true;
		if ( prevWid == h_sales && wids[j] == h_end) return true;
		if ( prevWid == h_sales && wids[j]==h_begin) return true;
		if ( prevWid == h_sales && wids[j]==h_start) return true;
		if ( prevWid == h_phone && wids[j]==h_hours) return true;
		prevWid = wids[j];
	}
	return false;
}

bool Dates::isFuneralDate ( int32_t a , int32_t b ) {
	int64_t prevWid = 0LL;
	// now scan forward from there
	for ( int32_t j = a ; j < b ; j++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip punct words
		if ( ! m_wids[j] ) continue;
		// is it register?
		if ( m_wids[j] == h_funeral    ) return true;
		if ( m_wids[j] == h_mortuary   ) return true;
		if ( m_wids[j] == h_visitation ) return true;
		if ( prevWid == h_memorial && m_wids[j] == h_services ) 
			return true;
		prevWid = m_wids[j];
	}
	return false;
}

bool Dates::isCloseHeader ( Section *si ) {
	if ( ! si ) return false;
	int32_t a = si->m_a;
	int32_t b = si->m_b;
	// now scan forward from there
	for ( int32_t j = a ; j < b ; j++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip punct words
		if ( ! m_wids[j] ) continue;
		// is it register?
		if ( m_wids[j] == h_close  ) return true;
		if ( m_wids[j] == h_closed ) return true;
		if ( m_wids[j] == h_closes ) return true;
		if ( m_wids[j] == h_closure) return true;
	}
	return false;
}

// add our votes to the new section voting table
bool Dates::addVotes ( SectionVotingTable *nsvt ) {

	// . ok, now rank the pub dates in order of most probable to least
	// . the date with the smallest penalty wins
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get flag
		dateflags_t flags = di->m_flags;
		// . must be like a clock or today's date
		// . either it has an hour or a month
		if ( ! (di->m_hasType & DT_TOD) &&
		     ! (di->m_hasType & DT_MONTH) )
			continue;
		// get date's section
		Section *sn = di->m_section;
		// must be there, not date from url
		if ( ! sn ) continue;
		// . put in our votes for clock
		// . stores into Sections::m_nsvt which is added to Sectiondb
		//   when XmlDoc calls Sections::hash()
		if ( flags & DF_CLOCK ) {
			if ( ! nsvt->addVote1(sn,SV_CLOCK,1.0) ) return false;
		else 
			if ( ! nsvt->addVote1(sn,SV_CLOCK,0.0) ) return false;
		}
		// we put in our vote like this now
		// set SEC_* flags for date format types
		if ( ! (flags & DF_AMBIGUOUS) && (flags & DF_MONTH_NUMERIC) ) {
			// put in our vote for european
			if ( flags & DF_EUROPEAN  ) 
				if ( ! nsvt->addVote1(sn,SV_EURDATEFMT,1.0) )
					return false;
			// put in our vote for american
			if ( flags & DF_AMERICAN  ) 
				if ( ! nsvt->addVote1(sn,SV_EURDATEFMT,0.0) )
					return false;
		}
	}


	//////////////// 
	//
	// . add votes for dates being in future/past/current time
	// . then in order to add an event whose date is within 24 hrs of
	//   the current time we must have had other dates in that section
	//   with the SV_FUTURE_DATE set, to be sure it is not a comment date
	//   or a clock date.
	//
	////////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// must have a valid timestamp then!
		if ( ! di->m_timestamp ) continue;
		// the section flag type
		int32_t sectionType = SV_CURRENT_DATE;
		// past, current or future?
		if ( di->m_timestamp < (time_t)m_nd->m_spideredTime - 24*3600 )
			sectionType = SV_PAST_DATE;
		if ( di->m_timestamp > (time_t)m_nd->m_spideredTime + 24*3600 )
			sectionType = SV_FUTURE_DATE;
		// . get section that contains this date's first component
		// . might be a telescoped date that touches several sections
		Section *sn = di->m_section;
		// the diff
		int32_t delta = di->m_timestamp - (time_t)m_nd->m_spideredTime;
		// set the appropriate bit
		if ( ! nsvt->addVote1(sn,sectionType,delta) ) return false;
	}

	return true;
}

// setting the Addresses needs the D_IS_IN_DATE bit we set above, so we
// have to interlace these functions!
bool Dates::setPart2 ( Addresses *aa , int32_t minPubDate , int32_t maxPubDate ,
		       // the old one - we read from that
		       //SectionVotingTable *osvt ,
		       bool isXml ,
		       bool isSiteRoot ) {
		       // the root doc voting table, should always be there
		       //HashTableX *rvt ) {

	m_isXml      = isXml;
	m_isSiteRoot = isSiteRoot;

	// a quick one
	if ( m_words->m_numWords == 0 ) return true;

	// make sure implied sections were added
	if ( ! m_sections->m_addedImpliedSections ) { char *xx=NULL;*xx=0; }

	// 
	// . set SF_RECURRING_DOW based on table headers
	// . fix for daily schedules
	//
	for ( int32_t x = 0 ; x < m_numDatePtrs ; x++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[x];
		// skip if none
		if ( ! di ) continue;
		// skip if not in the document body (i.e. in url itself)
		if ( di->m_a < 0 ) continue;
		if ( di->m_type != DT_DOW ) continue;
		if ( ! di->m_section ) continue;
		// it's not recurring if in a classical calendar for
		// a month, with daynums...
		if ( di->m_calendarSection ) continue;
		if ( di->m_flags&(DF_TABLEDATEHEADERROW|DF_TABLEDATEHEADERCOL))
			di->m_suppFlags |= SF_RECURRING_DOW;
	}

	// list take preference over compounds to fix April 2011 in
	// terrence wilson's calendar widget from compound to "SUN" first
	// dow in calendar header.
	// but now we have to apply the section logic in makeCompounds()
	// to addLists() as well
	if ( ! addLists ( m_words ,
			  true  ) )  // ignoreBreakingTags?
		return false;

	// try linking dates in same sentences now
	if ( ! makeCompounds ( m_words , 
			       false ,   // monthDayOnly?
			       true  ,   // linkDatesInSameSentence?
			       false ) ) // ignoreBreakingTags
		return false;


	// now call it again and ignore tags this time
	// only do this after we have implied sections
	// otherwise we get bad pairing of dates from different sections
	// that do not belong together!
	// "Nov 13-15 (last showing) 1pm,3pm<br><br>Nov 25th no showing"
	// ends up paring 1pm,3pm with Nov 25th!! which is wrong!!
	// for http://www.glsc.org/visit/omnimax.php?id=45
	// That is why we need implied sections before calling makeCompounds()
	// with ignoreBreakingTags set to true.
	if ( ! makeCompounds ( m_words , 
			       false ,   // monthDayOnly?
			       false ,   // linkDatesInSameSentence?
			       true ) ) // ignoreBreakingTags
		return false;

	// try to make lists again them on these new compounds
	if ( ! addLists ( m_words ,
			  true  ) )  // ignoreBreakingTags?
		return false;

	// . try to make ranges as well on these new compounds
	// . fixes activedatax.com:
	//   "start date: 10/7/2011 start time: 6:00pm</tr>
	//    end date: 10/7/2011 end time: 11:59pm"
	if ( ! addRanges ( m_words ) ) 
		return false;

	// and try to compound again?
	if ( ! makeCompounds ( m_words , 
			       false ,   // monthDayOnly?
			       false ,   // linkDatesInSameSentence?
			       true ) ) // ignoreBreakingTags
		return false;

	// set Date::m_compoundSection to possible be bigger than 
	// Date::m_section so that it holds all dates in a compound date
	// that may span multiple sentence sections
	for ( int32_t i = 0 ; i < m_numTotalPtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_totalPtrs[i];
		// skip if no ptrs
		if ( di->m_numPtrs <= 0 ) continue;
		// and in body
		if ( di->m_a < 0 ) continue;
		// get left and right most points
		int32_t mina = 99999999;
		int32_t maxb = -1;
		for ( int32_t k = 0 ; k < di->m_numPtrs; k++ ) {
			Date *pd = di->m_ptrs[k];
			if ( pd->m_a < mina ) mina = pd->m_a;
			if ( pd->m_b > maxb ) maxb = pd->m_b;
		}
		// get section date is in, if any
		Section *sa = m_sections->m_sectionPtrs[mina];
		// blow up until contains maxb-1
		for ( ; sa ; sa = sa->m_parent ) {
			if ( sa->m_b >= maxb ) break;
			if ( ! sa->m_parent ) break;
		}
		// sanity check
		di->m_compoundSection = sa;
	}


	// save it
	m_addresses = aa;
	//m_osvt      = osvt;
	//m_rvt       = rvt;


	//
	// for setting SEC_DATE_LIST_CONTAINER bit we need to first set
	// Section::m_firstDate
	//
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		if ( ! di ) continue;
		// get its a
		if ( di->m_a < 0 ) continue;
		// section ptr from that
		//Section *sp = m_sections->m_sectionPtrs[di->m_a];
		// fix for thealcoholenthusiast.com bug (see below)
		Section *sp = di->m_section;
		// grab it otherwise
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop when we got one
			if ( sp->m_firstDate ) break;
			// . otherwise, claim it
			// . add one since it is initialized to zero and zero
			//   is a legit "i" value actually
			sp->m_firstDate = i+1;
		}
	}
	// validate it
	m_sections->m_firstDateValid = true;


#define LT_YEAR       0x01
#define LT_MONTHDAY   0x02
#define LT_YEARMONDAY 0x04
#define LT_YEARMONTH  0x08

	///////////////////////
	// 
	// set SEC_DATE_LIST_CONTAINER bits
	//
	// fixes dates telescoping to other dates that are in a list
	// like in an archive menu "December 2009, ..." 
	// or daynum dates in a calendar section.
	//
	///////////////////////
	for ( Section *si = m_sections->m_rootSection ; si ; si = si->m_next) {
		// breathe
		QUICKPOLL(m_niceness);
		// must be in a brother list
		if ( ! si->m_nextBrother ) continue;
		// and first in the list
		if ( si->m_prevBrother ) continue;
		// reset stuff
		int32_t lastbv = 0;
		bool matched = false;
		// scan the brother list
		for ( Section *bro = si; bro ; bro = bro->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// . skip if empty
			// . fix for santafe.com whose daynum list ends in
			//   empty sections
			if ( bro->m_firstWordPos <= 0 ) continue;
			// if we have a tod anywhere, that's a no-go for list,
			// allow the datebrothers algo to do its thing
			//if ( bro->m_flags & SEC_HAS_TOD ) {
			//	matched = false;
			//	break;
			//}
			// . scan the dates in here
			// . subtract one since we added it above
			int32_t ii = bro->m_firstDate - 1;
			// skip if has no date we are interested in
			if ( ii < 0 ) { lastbv = 0; continue; }
			// reset bit vector for computing it for this brother
			int32_t bv = 0;
			// . loop over them
			// . CAUTION some dates like "1</b><sup<b>st</b></sup>"
			//   can span two brother sections
			for ( ; ii < m_numDatePtrs ; ii++ ) {
				// breathe
				QUICKPOLL(m_niceness);
				// get date 
				Date *di = m_datePtrs[ii];
				// skip if empty
				if ( ! di ) continue;
				// stop if passed our end
				if ( di->m_a >= bro->m_b ) break;
				// ignore if fuzzy
				if ( di->m_flags & DF_FUZZY ) continue;
				// sanity
				if ( di->m_a < bro->m_a ){
					// this causes
					// "1</b><sup<b>st</b></sup>" to core
					// dump because the <sup> section
					// is a brother section. so if a date
					// is split across brother sections
					// then we most likely do NOT have
					// a date container list, so we can
					// stop. this was for the url:
					// http://www.krwg.org/cevents.html
					break;
					//char *xx=NULL;*xx=0;}
				}
				// get type
				datetype_t dt = di->m_hasType;
				// take out compound flag
				dt &= ~DT_COMPOUND;
				// and dow
				dt &= ~DT_DOW;
				// . only deal with certain types
				// . "December 2008" for archive lists
				// . "Sunday, Sep 12" for southgatehouse.com
				// . 
				char tt = 0;
				if ( dt == DT_YEAR ) 
					tt = LT_YEAR;
				if ( dt == (DT_MONTH|DT_DAYNUM) )
					tt = LT_MONTHDAY;
				if ( dt == (DT_MONTH|DT_DAYNUM|DT_YEAR) )
					tt = LT_YEARMONDAY;
				if ( dt == (DT_MONTH|DT_YEAR) )
					tt = LT_YEARMONTH;
				if ( dt == (DT_MONTH|DT_YEAR) )
					tt = LT_YEARMONTH;
				if ( ! tt ) continue;
				// set our bit vector
				bv |= tt;
				// compare to last bit vector
				if ( ! ( lastbv & bv ) ) continue;
				// got one!
				matched = true;
				break;
			}
			// save it
			lastbv = bv;
		}
		// bail out if not a list of dates
		//if ( count <= 1 ) continue;
		if ( ! matched ) continue;
		// i guess make exact to fix adobetheater.org which is losing
		// its dom range because a couple of his brothers have dates!
		// AW! but dom ranges will be allowed to freely telescope out
		// as headers soon, so remove this constraint. that should
		// fix stoart.com now, well at least stoart seems to have
		// the right times, just not location.
		//if ( total != count ) continue;
		// get the parent
		if ( ! si->m_parent ) continue;
		// set parent's bit
		si->m_parent->m_flags |= SEC_DATE_LIST_CONTAINER;

		// telescope this up to fix uillinois.edu which has a
		// table row where one td is a list of month/daynums and
		// gets datelistcontainer bit set, and the other td cell
		// is a list of dows that visually align with the month/daynums
		// and were telescoping out of the table to the meeting date
		// of 8am completely ignoring the month/daynums they were
		// visually aligned with. so once any section contains
		// a date list, then no other date, even if not directly in
		// that date list, can escape...
		Section *sp = si->m_parent;
		for ( ; sp ; sp = sp->m_parent ) {
			QUICKPOLL(m_niceness);
			sp->m_flags |= SEC_DATE_LIST_CONTAINER;
		}
	}





	// int16_tcut
	wbit_t *bb = m_bits->m_bits;
	
	// invalidate daynum dates that are in addresses!
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if not single daynum
		if ( di->m_type != DT_DAYNUM ) continue;
		// skip if not in body
		if ( di->m_a < 0 ) continue;
		// skip if not in address
		if ( ! ( bb[di->m_a] & D_IS_IN_ADDRESS ) ) continue;
		// ok, it is bad!
		di->m_flags |= DF_FUZZY;
	}

	// and mont daynum ranges that are in a hyperlink and have the
	// word festival in the same hyperlink are probably talking about
	// a festival on another page, so consider these dates to be
	// fuzzy as well
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// must be month daynum range
		if ( ! ( di->m_hasType & DT_MONTH  ) ) continue;
		if ( ! ( di->m_hasType & DT_DAYNUM ) ) continue;
		// init flags
		bool gotFestival = false;
		// now if the potential header date is in a hyperlink, that
		// disqualifies it as well! because it is referring to
		// another page... this fixes
		// http://texasdrums.drums.org/albuquerque.htm
		int32_t k = di->m_a;
		// set min for scan
		int32_t kmin = di->m_a - 30;
		// do not leave section. no! could be <a><span>...
		//if ( kmin < si->m_a ) kmin = si->m_a;
		// assume not in an href tag
		bool inAnchorTag = false;
		// do not go negative
		if ( kmin < 0 ) kmin = 0;
		// scan for the anchor tag
		for ( ; k >= kmin ; k-- ) {
			// stop if back anchor tag
			if ( m_tids[k] == (TAG_A | BACKBIT) ) break;
			// set this?
			if ( m_wids[k] == h_festival ) gotFestival = true;
			// skip if not front anchor tag
			if ( m_tids[k] != TAG_A ) continue;
			// we got a front tag
			inAnchorTag = true;
			break;
		}
		// try next date if not in anchor tag
		if ( ! inAnchorTag ) continue;
		// set max of scan
		int32_t kmax = di->m_b + 30;
		// limit max
		if ( kmax > m_nw ) kmax = m_nw;
		// scan forward for festival if we need to
		for ( k = di->m_b ; k < kmax ; k++ ) {
			// stop if end of href tag
			if ( m_tids[k] == (TAG_A|BACKBIT) ) break;
			// mark it
			if ( m_wids[k] != h_festival ) continue;
			// mark it
			gotFestival = true;
			break;
		}
		// skip date if no festival
		if ( ! gotFestival ) continue;
		// mark it as fuzzy
		di->m_flags |= DF_FUZZY;
	}	


	/*
	//
	// before telescoping, identify "veritcal lists" of dates,
	// like how stoart.com has 3 dows in a vertical list and tries
	// to align them by hand to tods in a table column to their right.
	//
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if nuked from range logic above
		if ( ! di ) continue;
		// skip if from url or not otherwise in the body
		if ( di->m_a < 0 ) continue;
		// need this
		Date *lastDate = di;

		// init the "j" loop
		int32_t j = i + 1 ;
		// int16_tcut
		Date *dj = NULL;
		// scan over atomic dates after the month
		for ( ; j < m_numDatePtrs ; j++ ) {
			// get the jth atomic date
			dj = m_datePtrs[j];
			// skip if nuked (was endpoint of a range i guess)
			if ( ! dj ) continue;
			// skip if from url or not otherwise in the body
			if ( dj->m_a < 0 ) continue;
			// does it match?
			bool match = (dj->m_hasType == di->m_hasType);
			// but do allow "Dec 13,20" to match "Jan 3"
			if ( ! match &&
			     di->m_hasType ==
			     (DT_COMPOUND|DT_LIST_DAYNUM|DT_MONTH|DT_DAYNUM) &&
			     dj->m_hasType ==
			     (DT_COMPOUND|               DT_MONTH|DT_DAYNUM) )
				match = true;
			// and vice versa
			if ( ! match &&
			     dj->m_hasType ==
			     (DT_COMPOUND|DT_LIST_DAYNUM|DT_MONTH|DT_DAYNUM) &&
			     di->m_hasType ==
			     (DT_COMPOUND|               DT_MONTH|DT_DAYNUM) )
				match = true;
			// stop if not a day of month
			if ( ! match ) break;
			// assume not veritcal
			bool vertical = false;
			// declare outside for loop
			int32_t a = lastDate->m_b;
			// scan words in between date "di" and date "dj"
			for ( ; a < dj->m_a ; a++ ) {
				// stop on any alnum
				if ( m_wids[a] ) break;
				// check the tag id
				if ( m_tids[a] ) {
					// see if vertical/breaking tag or not
					if ( isBreakingTagId(m_tids[a]) )
						vertical = true;
					continue;
				}
				// scan the punct word otherwise
				char *p    = m_wptrs[a];
				char *pend = p + m_wlens[a];
				for ( ; p < pend ; p += getUtf8CharSize(p) ) {
					// space is ok
					if ( *p == ' ' ) continue;
					// other whitespace is ok
					if ( is_wspace_utf8(p) ) continue;
					// all else stops the vertical list
					break;
				}
				// stop if punct word breaks the veritcal list
				if ( p < pend ) break;
			}
			// if allowable junk between di and dj, add day to list
			if ( a < dj->m_a ) break;
			// need a breaking tag...
			if ( ! vertical ) break;
			// ok, we got a veritcal list, keep going
			di->m_flags |= DF_IN_VERTICAL_LIST;
			dj->m_flags |= DF_IN_VERTICAL_LIST;
			// measure from this
			lastDate = dj;
		}
		// advance i over the list we just made
		i = j - 1;
	}
	//
	// done identifying vertical lists to fix stoart.com
	//
	*/
	
	///////////////////////////////
	//
	// set DF_COMMENT_DATE flag
	//
	///////////////////////////////
	/*
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// skip if not in body
		if ( di->m_a < 0 ) continue;
		// get date's section. section containing first date in the
		// case of telescoped dates
		Section *sn = di->m_section;
		// is it close to the current time? it could be a comment date
		// or a clock date...
		if ( di->m_timestamp <= 0 ) continue;
		// if well into the future it can't be a comment then...
		if ( di->m_timestamp >= m_nd->m_spideredTime+24*3600) continue;
		// int16_tcut
		int32_t th = sn->m_tagHash;
		// otherwise, it is close to the current time so check it
		// to see if it is a probable comment date or clock date
		if ( m_sections->getNumSampled(th,SV_FUTURE_DATE)>=1) continue;
		// . fix burtstikilounge.com.
		// . if date is of format 23 [[]] November 2009 ... allow it
		// . these date formats are not "comments" per se or article
		//   pub dates
		if ( di->m_hasType & DT_RANGE_ANY )
			continue;
		if ( di->m_hasType & DT_LIST_ANY )
			continue;
		// ok, probably a comment date
		di->m_flags |= DF_COMMENT_DATE;
	}
	*/

	//////////////////////////////
	//
	// set DF_PUB_DATE
	//
	//////////////////////////////
	int64_t lastWid = 0LL;
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// check out words before it
		int32_t a = di->m_a;
		// skip if not in body
		if ( a < 0 ) continue;
		// get section
		Section *sd = m_sections->m_sectionPtrs[a];
		// get sentence
		Section *ss = sd->m_sentenceSection;
		// if no sentence we must be in javascript
		if ( m_contentType == CT_JS && ! ss ) continue;
		// get sentence start
		int32_t sa = ss->m_a;
		// stop here
		int32_t amin = a - 20; if ( amin < 0 ) amin = 0;
		int32_t alnumCount = 0;
		// reset
		lastWid = 0LL;
		// skip date for scan
		a--;
		// scan backwards
		for ( ; a >= amin ; a-- ) {
			// tag?
			if ( m_tids[a] ) {
				// skip if not xml tag
				if ( m_tids[a] != TAG_XMLTAG ) continue;
				// pt to tag name
				char *tn = &m_wptrs[a][1];
				// is it "pubdate" like trumba rss feed?
				if ( ! strncasecmp(tn,"pubdate",7) ) break;
				if ( ! strncasecmp(tn,"published",9) ) break;
				// otherwise, just skip over it
				continue;
			}
			// skip if punct word
			if ( ! m_wids[a] ) continue;
			// preserve this
			int64_t saved = lastWid;
			// update it for an easy continue
			lastWid = m_wids[a];
			// count it
			alnumCount++;
			// "posted oct 24, 2011"
			if ( m_wids[a] == h_posted && saved == 0LL )
				break;
			// "last updated"
			if ( m_wids[a] == h_last && saved == h_updated )
				break;
			// "last modified"
			if ( m_wids[a] == h_last && saved == h_modified )
				break;
			// "modified at"
			if ( m_wids[a] == h_modified && saved == h_at )
				break;
			if ( m_wids[a] == h_modified && saved == h_on )
				break;
			// "updated at"
			if ( m_wids[a] == h_updated && saved == h_at )
				break;
			if ( m_wids[a] == h_updated && saved == h_on )
				break;
			// By so-and-so [on] Nov 24, 2009 (peachpundit.com)
			if ( m_wids[a] == h_by && a == sa && alnumCount >= 2 ) 
				break;
			// posted on
			if ( m_wids[a] == h_posted && saved == h_on )
				break;
			// just updated
			if ( saved == h_updated )
				break;
		}
		// set it?
		if ( a > amin ) di->m_flags |= DF_PUB_DATE;
	}

	setDateHashes();

	// set SEC_HAS_REGISTRATION bits on the section flags
	m_sections->setRegistrationBits ( ) ;

	///////////////////////
	//
	// set DF_REGISTRATION on the date flags
	//
	// . needed by setTODXors()
	//
	///////////////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// start telescoping at date's section
		Section *sd = di->m_section;
		// telescope all the way up
		for ( ; sd ; sd = sd->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// skip it?
			if ( !(sd->m_flags & SEC_HAS_REGISTRATION)) continue;
			// mark it
			di->m_flags |= DF_REGISTRATION;
			// and no need to go further with this date
			break;
		}
	}
			

	setPhoneXors  ();
	setEmailXors  ();
	setPriceXors  ();
	setTODXors    ();
	m_sections->setAddrXors ( m_addresses );


	/////////////////////////////
	//
	// . set DF_WEEKLY_SCHEDULE 
	// . set DF_STORE_HOURS
	// 
	// . store hours = the times when the store is open
	//
	/////////////////////////////
	setStoreHours ( false );

	///////////////////////
	// 
	// set SEC_EVENT_BROTHER bits (PART 1)
	//
	// set for lists of brothers where two
	// or more brothers both have a dom/dow and a tod. basically, this
	// is trying to see that they are talking about different events,
	// and we should consider each brother to be a separate event, and
	// not allow date telescoping between brothers then. we should
	// set this bit right before doing telescoping i guess.
	//
	///////////////////////
	//
	// . also we call this again after doing telescopes and recomputing
	//   date xors to fix burtstikilounge.com which has fuzzy daynums in 
	//   each table cell that will telescope out to the month/year. only 
	//   then will the eventbrotherbits be set for the calendar day cells.
	setEventBrotherBits();


	// . before doing part 8, create "buf"
	// . telescope each date up to all its parent sections
	// . if a section ends up containing multiple dates then indicate
	//   that by setting the section's date ptr to -2
	// . use -1 to indicate no date at all
	// . use -2 to indicate 2+ dates
	// . otherwise store the m_datePtrs[] index in here
	// . "buf" is 1-1 with the sections
	char  tmp[20000];
	char *tbuf     = tmp;
	int32_t  tbufSize = 20000;
	// just allocate if we can not fit it in the stack
	if ( m_sections->m_numSections > 2000 ) { 
		tbuf     = NULL; 
		tbufSize = 0; 
	}

	// set it
	HashTableX ht;
	if ( ! ht.set ( 4 , 
			4 , 
			m_sections->m_numSections, 
			tbuf ,
			tbufSize,
			true, // allow dups?? yes!!
			m_niceness , 
			"ht-datesec" ) )
		return false;
	// breathe
	QUICKPOLL ( m_niceness );

	///////////////////////////////////////
	//
	// make the list of header candidates for telescoping
	//
	///////////////////////////////////////

	// i think the isCompatible() function handles the logic i commented
	// out above. so let's just make sure that "ht" maps a section ptr
	// to all the dates that that section contains!
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// this is for telescoping which we have not done yet!
		if ( di->m_hasType & DT_TELESCOPE ) { char *xx=NULL;*xx=0; }
		// ignore if fuzzy though! ht is used to find possible headers
		// for a date
		if ( di->m_flags & DF_FUZZY ) continue;
		// not allowed to use copyright years as header
		if ( di->m_flags & DF_COPYRIGHT ) continue;
		// no timestamps can be headers
		if ( di->m_hasType & DT_TIMESTAMP ) continue;
		// if date is a non-event date, like "rsvp by" or
		// "deadline: " or ticket hours, etc. do not let it be a header
		//if ( di->m_flags & DF_NONEVENT_DATE ) continue;
		// . do not allow pubdates to be used as date headers
		// . fixes abqtango.com which has "Updated September 24, 2009"
		//   at the bottom of the page so we were telescoping to that!
		if ( di->m_flags & DF_PUB_DATE ) continue;
		// must be in body
		if ( di->m_a < 0 ) continue;
		// get our section
		Section *si = m_sections->m_sectionPtrs[di->m_a];
		// . menu items can not be headers
		// . tried to fix 
		//   http://www.newmexicomusic.org/news.php?select=843 using
		//   this logic, which has a festival month & daynum range in,
		//   its menu, but it turns out that this was not a menu item
		//   because it was not in a list of other menu items...
		//   it was basically by itself...
		// . this hurts 
		//   newmexico.org/calendar/events/index.php?lID=1781 because
		//   it has a menu that consists of events basically! and 
		//   at the top of the menu is the header date and each event
		//   in the menu list has a TOD
		//if ( si->m_flags & SEC_IS_MENUITEM ) continue;
		// don't allow dates in scripts, seem to be a lot of
		// non fuzzy "daynums" that are just function args
		if ( si->m_flags & SEC_SCRIPT ) continue;
		// or dates in <noscript> tags
		if ( si->m_flags & SEC_NOSCRIPT ) continue;

		// do not allow daynum ranges to be headers
		// fixes "daily 9 - 6 [[]] 4-16 [[]] Apr 1 - Nov 1" for
		// http://www.collectorsguide.com/ab/abmud.html
		if ( di->m_type == DT_RANGE_DAYNUM ) continue;

		// no! http://www.zvents.com/albuquerque-nm/events/show/88543421-the-love-song-of-j-robert-oppenheimer-by-carson-kreitzer has some good
		// dow/month/daynum headers in a hyperlink, and that is messing
		// it up pretty bad. we just need to modify isCompatible()
		// to prevent that one festival hyperlink from being a header
		// by noting that it contains a big menu???? we need another
		// approach anyway...

		/*
		// now if the potential header date is in a hyperlink, that
		// disqualifies it as well! because it is referring to
		// another page... this fixes
		// http://texasdrums.drums.org/albuquerque.htm
		int32_t k = di->m_a;
		// set min for scan
		int32_t kmin = di->m_a - 30;
		// do not leave section. no! could be <a><span>...
		//if ( kmin < si->m_a ) kmin = si->m_a;
		// assume not in an href tag
		bool inAnchorTag = false;
		// do not go negative
		if ( kmin < 0 ) kmin = 0;
		// scan for the anchor tag
		for ( ; k >= kmin ; k-- ) {
			// stop if back anchor tag
			if ( tids[k] == (TAG_A | BACKBIT) ) break;
			// skip if not front anchor tag
			if ( tids[k] != TAG_A ) continue;
			// we got a front tag
			inAnchorTag = true;
			break;
		}
		// do not allow it as a header if in a link
		if ( inAnchorTag ) {
			// mark it for printing
			di->m_flags |= DF_IN_HYPERLINK;
			// save this
			lastDate = di;
			continue;
		}
		*/

		// if in a sentence with another date then do not allow it
		// to be the header with any date outside of that sentence.
		// should fix http://www.collectorsguide.com/ab/abmud.html
		// which has
		// "Open every day of the week, 9am-5pm. Extended hours 
		//  Saturdays and Sundays in summer: 9am-6pm."
		// and was using "9am-5pm[[]]Saturdays and Sundays" as a date.
		// crap! for panjea.org the shona language class, 8:30-9pm
		// was not telescoping to TUESDAYS because of this algo. it
		// it probably better to let a telescope happen rather than
		// none, so at least the date is more constrictive. better
		// tighter than looser because it was be so obviously wrong
		// when people look for events.
		// the fix for the collectorsguid.com is to support
		// "every day" as a date, like a Sunday-Saturday DOW range.
		//if(lastDate && lastDate->m_sentenceId == di->m_sentenceId ) {
		//	// it can still be a header, but we need to only
		//	// allow it to pair up with other dates in the same
		//	// sentence
		//	di->m_flags       |= DF_IN_SAME_SENTENCE;
		//	lastDate->m_flags |= DF_IN_SAME_SENTENCE;
		//}
		// update
		//lastDate = di;


		// loop over section and all parents
		for ( ; si ; si = si->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// TODO: make this scalable!!!!!!!!!
			if ( ! ht.addKey(&si,&i)){char *xx=NULL;*xx=0;};
			// . store hours can telescope out
			// . this hurts graffiti.org since we end up 
			//   telescoping out event dates that are just
			//   daymonth ranges to store hours from other date
			//   brothers. "Nov 27  - Dec 18 [[]] store hrs"
			if ( di->m_flags & DF_STORE_HOURS ) continue;
			// date brother boundary
			if ( si->m_flags & SEC_EVENT_BROTHER ) break;
			if ( si->m_flags & SEC_DATE_LIST_CONTAINER ) break;
		}
	}
	/*
	datetype_t specialTypes = 
		DT_HOLIDAY      | // thanksgiving
		DT_SUBDAY       | // mornings
		DT_SUBWEEK      | // weekends
		DT_SUBMONTH     | // last day of month
		DT_EVERY_DAY    | // 7 days a week
		DT_SEASON       | // summers
		DT_ALL_HOLIDAYS ; // "holidays"
	*/

	// since below we use m_totalPtrs[] now we must ignore the dates
	// that the telescope ignores. these are set to NULL in m_datePtrs.
	sec_t badFlags =SEC_MARQUEE|SEC_STYLE|SEC_SCRIPT|SEC_SELECT|
		SEC_HIDDEN|SEC_NOSCRIPT;

	//
	// . set Section::m_dateBits to all date types it contains
	// . replaces uses of "ht" in isCompatible()
	// . should speed up a lot
	// . gotta use totalPtrs since now dates can span multiple sections
	// . yeah, we were messing up on rialtopool.com because the
	//   list logic was pairing up Friday and Saturday with 
	//   "First Thursdays" this only dateBits of First Thursdays section
	//   were getting set, thereby allowing the 10pm in Friday and 
	//   Saturdays section to telescope up to other DOWs.
	//
	for ( int32_t i = 0 ; i < m_numTotalPtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_totalPtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// must be in the doc
		if ( di->m_a < 0 ) continue;
		// is this even set right now?
		if ( di->m_hasType & DT_TELESCOPE ) continue;//break;

		// skip if in bad section
		Section *si = di->m_section;
		// skip if none
		if ( ! si ) continue;
		// skip if not bad
		if ( si->m_flags & badFlags ) continue;

		// skip fuzzies. fixes reverbnation.com somehow. thought the
		// "5" in the band name was a daynum
		if ( di->m_flags & DF_FUZZY ) continue;
		// . skip if ticket or registration date
		// . help fix signmeup.com so the race tod start times can
		//   telescope to the big dayof month date at top of page
		//   and not worry about the ticket/registration dates near it
		if ( di->m_flags & DF_NONEVENT_DATE ) continue;
		if ( di->m_flags & DF_REGISTRATION  ) continue;
		if ( di->m_flags5 & DF5_IGNORE ) continue;
		// get its bits
		datetype_t dt = di->m_hasType;
		/*
		// convert a holiday like thanksgiving to a month and daynum
		// everything except generic "holiday" word
		if ( dt & specialTypes ) {
			// int16_tcut
			suppflags_t sf = di->m_suppFlags;
			// remove the bit
			//dt &= ~specialTypes;
			// add in a day bit if not "holidays" word
			// fix http://www.wpd4fun.org/Events/Halloween.htm
			// do not let "on halloween" be considered a daynum
			// becuse that is preventing our 
			// "todrange[[]]hallowwen" date from telescoping to 
			// "oct 21, 2010" because we treat halloween as a 
			// daynum, so try fixing this...
			// but i think this is losing a turkey trot because
			// it telescopes up to a bad date..

			if ( (dt & DT_EVERY_DAY) ||
			     (dt & DT_SUBMONTH)  ||
			     // without subday we were getting
			     // "10pm [[]] Monday nights [[]] January 10, 2011"
			     // for zipscene.com dj johnny b when it should
			     // not have been able to telescope to
			     // "January 10, 2011"
			     ((dt & DT_SUBDAY) && (sf & SF_RECURRING_DOW)) )
				dt |= DT_DAYNUM;
			//dt |= DT_DAYNUM;	
		//if ( ! ( dt & DT_ALL_HOLIDAYS ) ) 
			//	dt |= DT_DAYNUM;
		}
		*/
		//if ( dt & DT_HOLIDAY ) {
		//	// take it out
		//	dt &= ~DT_HOLIDAY;
		//	// put something else in its place maybe
		//	if ( di->m_suppFlags & SF_NORMAL_HOLIDAY ) {
		//		//dxtype |= DT_MONTH|DT_DAYNUM;
		//		dt |= DT_DAYNUM;
		//	}
		//	// everything except generic "holiday" word
		//	else if ( di->m_num != HD_HOLIDAYS ) {
		//		dt |= DT_DAYNUM;
		//	}
		//}
		// telescope it into all sections
		Section *sd = m_sections->m_sectionPtrs[di->m_a];
		for ( ; sd ; sd = sd->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if already have 
			if ( (sd->m_dateBits & dt) == dt ) break;
			// otherwise, or in
			sd->m_dateBits |= dt;
		}
	}
	// mark them valid
	m_dateBitsValid = true;

	//
	// A CONTINUATION of the DF_CLOSE_DATE algo above. but we need
	// m_dateBits to be valid so its down here. we might have implied
	// sections valid too which is good since we use sections here.
	//
	// sometimes they have a list of closure dates with the
	// heading "holiday office closures" like for asthmaallies.org!
	// so just scan backwards, skipping words that are dates and
	// see if we hit "closure", etc. basically the same algo in
	// setStoreHours()
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		int32_t a = di->m_a;
		int32_t min = a - 200;
		// limit to beginning of section, "ds"
		//if ( ds && min < ds->m_a ) min = ds->m_a;
		// and do not go below zero of course
		if ( min < 0 ) min = 0;
		// limit scan to 7 alnums back...
		int32_t alnumCount = 0;
		int64_t lastWid = 0LL;
		for ( ; a >= min ; a-- ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip tags and punct
			if ( ! m_wids[a] ) continue;
			// do not count dates in list towards alnumcount
			if ( bb && (bb[a] & D_IS_IN_DATE) ) continue;
			// limit alpha count
			if ( ++alnumCount >= 7 ) break;
			if ( m_wids[a] == h_closures ) break;
			if ( m_wids[a] == h_closure ) break;
			// stop to avoid breach
			if ( a - 2 < min ) continue;
			// will be closed ...
			if ( ( m_wids[a] == h_be ||
			       m_wids[a] == h_is ||
			       m_wids[a] == h_are ) && 
			     lastWid == h_closed ) 
				break;
			// will close...
			if ( m_wids[a] == h_will && lastWid == h_close ) break;
			// update this
			lastWid = m_wids[a];
		}
		// skip this date if no a close date
		if ( alnumCount >= 7 || a < min ) continue;
		// now get the section around the "closed" word
		Section *cs = m_sections->m_sectionPtrs[a];
		// blow it up, but stop when it hits a date
		for ( ; cs ; cs = cs->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// quicky
			if ( cs->m_dateBits ) break;
		}
		// if date is OUTSIDE of close section, do not set DF_CLOSE_DAT
		if ( cs && di->m_a >= cs->m_b ) continue;
		// flag it
		di->m_flags |= DF_CLOSE_DATE;
	}


	/*	
	/////////////////////////////
	//
	// set DF_ONOTHERPAGE
	//
	/////////////////////////////
	// . now use svt
	// . do this before telescoping because if one date in a telescope 
	//   does not have DF_ONOTHERPAGE set then we clear that bit for the
	//   telescope
	// . we hashed all the sections containing any tod ranges on the root
	//   page, assuming it to be store hours.
	// . we hash the tagid with its content hash for each section as we
	//   telescoped up hashing in more tagids, up to 5.
	// . so how many layers of the onion can we match? 
	// . should help us fix web pages that have the store hours repeated
	//   on every page so that just because the page might mention a
	//   location we do not apply the store hours to that location!!!
	for ( int32_t i = 0 ; m_osvt && i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// . but if we are the root page, there is no other page
		// . because if page gets respidered it will lose the event,
		//   that is, if the event date is repeated 2+ it will never
		//   be stored, unless it is on the site root page now.
		if ( m_isSiteRoot ) break;
		// and don't apply if we are xml either to fix dups in
		// the trumba.com feeds
		if ( m_isXml ) break;
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// must be in body
		if ( di->m_a < 0 ) continue;
		// . get the date root hash
		// . this is a hash of the section's content hash and its
		//   top 3 parent tagids
		//uint32_t modified = getSectionContentTagHash3(di->m_section);
		Section *si = di->m_section;
		if ( si->m_votesForDup > 0 ) 
			di->m_flags |= DF_ONOTHERPAGE;
		// it does! so set date flag as being from root
		//di->m_flags |= DF_ONROOTPAGE;
	}
	*/

	//////////////
	//
	// set Date::m_calendarSection for isCompatible() to use
	//
	///////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if nuked from range logic above
		if ( ! di ) continue;
		// int16_tcut
		Date *dj = di->m_lastDateInCalendar;
		// point to last one
		if ( ! dj ) continue;
		// save it
		Section *firstSec = m_sections->m_sectionPtrs[di->m_a];
		Section *lastSec  = m_sections->m_sectionPtrs[dj->m_a];
		// get calendar section
		Section *cs = NULL;
		for ( cs = lastSec ; cs ; cs = cs->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// stop if contains all
			if ( cs->contains ( firstSec ) ) break;
		}
		// scan above for month or month/year
		Date *monthDate = NULL;
		for ( int32_t k = i - 1 ; k >= 0 ; k-- ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// int16_tcut
			Date *dk = m_datePtrs[k];
			// skip if not there
			if ( ! dk ) continue;
			// dow list is ok
			if ( dk->m_type == DT_LIST_DOW ) continue;
			// month is what we want
			// sometimes it is compound with the damn list of dows
			datetype_t dt = dk->m_hasType;
			dt &= ~DT_COMPOUND;
			dt &= ~DT_DOW;
			dt &= ~DT_LIST_DOW;
			dt &= ~DT_YEAR;
			if ( dt == DT_MONTH )
				monthDate = dk;
			// otherwise break it
			break;
		}
		// sometimes the assholes put it below! mesaartscenter.com
		for ( int32_t k = i + 1 ; k < m_numDatePtrs ; k++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// stop if was above
			if ( monthDate ) break;
			// int16_tcut
			Date *dk = m_datePtrs[k];
			// skip if not there
			if ( ! dk ) continue;
			// dow list is ok
			if ( dk->m_type == DT_LIST_DOW ) continue;
			// month is what we want
			if ( dk->m_hasType == (DT_MONTH|DT_YEAR|DT_COMPOUND) ||
			     dk->m_hasType == (DT_MONTH) )
				monthDate = dk;
			// if in with all the daynums, allow it to slide,
			// because month will be outside of that set.
			if ( dk->m_a < cs->m_b ) continue;
			// otherwise break it
			break;
		}
		// get first date after cs->m_b
		Date *dx = NULL;
		for ( int32_t x = i + 1 ; x < m_numDatePtrs ; x++ ) {
			QUICKPOLL ( m_niceness );
			dx = m_datePtrs[x];
			if ( ! dx ) continue;
			// allow for month name after calendar daynums
			if ( dx == monthDate ) continue;
			if ( dx->m_a >= cs->m_b ) break;
		}
		// can "cs" capture the month without getting any more
		// dates outside of the current cs?
		for ( ; cs ; cs = cs->m_parent ) {
			QUICKPOLL ( m_niceness );
			if ( ! cs->m_parent ) break;
			// need monthDate of course
			if ( ! monthDate ) break;
			// not allowed to include "dx", a date after the
			// last daynum in the calendar...(daynum portion)
			if ( dx && cs->m_parent->m_b > dx->m_a ) break;
			// once we got month, that is good!
			if ( cs->contains2 ( monthDate->m_a ) ) break;
		}
		// flag them all! including the month/year if there
		for ( int32_t j = 0 ; j < m_numDatePtrs ; j++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// int16_tcut
			Date *dj = m_datePtrs[j];
			// skip if nuked from range logic above
			if ( ! dj ) continue;
			// must be in body
			if ( dj->m_a < 0 ) continue;
			// skip if not in calendar section
			if ( ! cs->contains2 ( dj->m_a ) ) continue;
			// set it now
			dj->m_calendarSection = cs;
		}
	}
	

	///////////////////////////////////////
	//
	// part 8
	//
	// telescope each date out to add in additional atoms from
	// differnet sections "above" our section
	//  
	// . be careful with ranges and lists
	// 
	// . telescope isolated numeric days (lists/ranges) out to find
	//   a month and a year.
	// . telescope canonical isolated months out to find a year
	// . year can be like 2009 or '09.
	// . telescope tods and wdays out likewise
	// . "<h1>2009</h1><h2>November</h2>...<h3>3rd</h3>" by using our
	//
	///////////////////////////////////////

	// TODO: what about when multiple dates in same section but they
	// each "modify" a different subsection:
	// i.e. "<div> D1 <div3>d3</div3> D2 <div4>d4</div4> </div>"

	// we mod this in the loop, so set it here and use "ndp" for the loop
	//int32_t ndp = m_numDatePtrs;

	// int16_tcut
	//dateflags_t msk = DT_DOW|DT_TOD|DT_MONTH|DT_YEAR|DT_DAYNUM;

	// we loop backwards now to set DF_USEDASHEADER flag right to fix
	// "<t1>dec... 7pm</t1><t2>nov.. 8pm</t2>" from producing
	// "nov 7pm" as a telescoped date
	// -- http://www.imbibenobhill.com/calendar/index.html
	// MDW: the new isCompatible() function fixes that case...
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// do not telescope if no need to!
		//if ( di->m_type  & DT_MONTHDAYYEAR ) continue;
		if ( di->m_hasType & DT_TIMESTAMP ) continue;
		// crap, the constraint was stopping 
		// "Friday, 13 November 2009" from telescoping to "7:30 PM"
		// when they were in the same table row, but adjacent columns
		// for http://events.mapchannels.com/Index.aspx?venue=628
		//if ( di->m_hasType & DT_YEAR         ) continue;
		if ( di->m_hasType & DT_TELESCOPE    ) break;//continue;
		// must be in body
		if ( di->m_a <= 0 ) continue;
		// "first" alone cannot telescope
		//if ( di->m_type == DT_MOD          ) continue;
		// . if used as a header, cannot telescope
		// . allow those guys to telescope now, then when done
		//   if we have conflicts we select the best telescoped
		//   chains
		// . i.e. if we have:
		// 8pm on the date of dec 11<br>
		// 6pm<br>
		// we will end up having "6pm [[]] dec 11" as well as
		// "dec 11 [[]] 8pm" and we should end up selecting
		// the latter and eliminating the former because it spans
		// less sections.
		// so basically this algorithm was working ok, but if things
		// got out of sync like that we needed to prefer
		// certain pairups over others, which was the point behind
		// getFirstParentOfType() function
		//if ( di->m_flags & DF_USEDASHEADER ) continue;
		// not allowed to telescope copyright years
		if ( di->m_flags & DF_COPYRIGHT ) continue;

		// skip pub dates
		if ( di->m_flags & DF_PUB_DATE  ) continue;

		// must be bookended on both ends to telescope out
		//if ( ( ! ( di->m_flags & DF_LEFT_BOOKEND  ) ||
		//       ! ( di->m_flags & DF_RIGHT_BOOKEND )   ) &&
		//     // this only applies to DOWs and DAYNUMS which are
		//   // especially problematic headers because of false matches
		//     // like "Joe Friday" or "Your First Practice"
		//     ( di->m_type == DT_DOW || di->m_type == DT_DAYNUM ) )
		//	continue;
		if ( di->m_flags & DF_FUZZY ) continue;

		// why are we isolated letting years and year ranges
		// telescope to something? that is strange...
		// fixes graffiti.org's 1999-1995 year telescoping out of
		// control to all the different ranges from the events.
		if ( di->m_hasType == DT_YEAR ) continue;
		if ( di->m_hasType == (DT_YEAR|DT_RANGE) ) continue;

		// do not allow holidays to be a base to telescope from.
		// fixes "Halloween [[]] 12:00 4:00pm" for 
		// http://www.wpd4fun.org/Events/Halloween.htm
		if ( //di->m_hasType == DT_HOLIDAY ||
		     di->m_hasType == DT_ALL_HOLIDAYS ||
		     di->m_hasType == DT_SUBDAY ||
		     di->m_hasType == DT_SUBWEEK ||
		     di->m_hasType == DT_SUBMONTH ||
		     di->m_hasType == DT_SEASON ||
		     di->m_hasType == DT_EVERY_DAY )
			continue;

		// or thanksgiving 2008
		if ( di->m_hasType == (DT_HOLIDAY|DT_YEAR|DT_COMPOUND) &&
		     !(di->m_suppFlags & SF_ON_PRECEEDS) )
			continue;
		// stop thanksgiving but not "on thanksgiving"
		if ( di->m_hasType == DT_HOLIDAY &&
		     !(di->m_suppFlags & SF_ON_PRECEEDS) )
			continue;

		// . do not telescope to date in a menu
		// . fixes http://www.residentadvisor.net/event.aspx?221238
		//   which is telescoping a "no rentry after 4am" to an
		//   event name in the menu: "2nd Sunday Nyc wit..". 
		if ( di->m_section && (di->m_section->m_flags & SEC_MENU) ) 
			continue;

		// to fix stoart.com, do not allow dates in vertical lists
		// to telescope to anything
		//if ( di->m_flags & DF_IN_VERTICAL_LIST ) continue;

		// this is for deduping date telescoping combintaions
		char cbuf[2000];
		HashTableX comboTable;
		comboTable.set(4,0,32,cbuf,2000,false,m_niceness,"datecombos");

		// reset this
		int32_t maxPtrs = 0;

		// repeat telescoping algo for the same date, "di" but
		// select different components this time
	repeat:
		// use this now, DD is the telescoping date
		Date *DD = NULL;
		Date *lastAddedPtr = NULL;
		Section *lastAddedSection = NULL;
		datetype_t lastHasType;
		bool hasMultipleHeaders = false;
		// default this
		Date *lastdi = di;
		//datetype_t accum = di->m_hasType;
		//Date *last = di;
		// int16_tcut
		//Section **ss = m_sections->m_sectionPtrs;

	redo:

		int32_t storeHoursCount = 0;
		// get our section
		Section *pp = m_sections->m_sectionPtrs[lastdi->m_a];
		// . grab parent
		// . no, allow telescoping to guys in same section now:
		//   "November 27, 8:30PM; (Doors open at 8:30 PM. Show 
		//    starts at 9:00 PM)"
		//pp = pp->m_parent;
		//int32_t alreadyAdded = 0;
		// set this
		//Section *lastpp = NULL;

		// loop over "pp" and all its parents
		for ( ; pp ; ) { // ; pp = pp->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// we no longer need this now because if we are
			// comaptible with a date we have to telescope to it,
			// we can't just skip it. the "s3" algo should fix
			// the rest of thewoodencow.com in isCompatible()
			//bool hasStoreHoursTwins = false;

			// save for this code
			//Section *savedLast = lastpp;
			// update it
			//lastpp = pp;

			// pick the one *right above* us
			int32_t slot = ht.getSlot ( &pp );
			// get best date that telescoped into this section
			Date *bestdp = NULL;
			int32_t  bestpn;
			int32_t  bestDist;
			// loop over all dates that telescoped up to this sec.
			for ( ; slot >= 0 ; slot = ht.getNextSlot(slot,&pp) ) {
				// get it
				int32_t pn = *(int32_t *)ht.getValueFromSlot(slot);
				// get the date index
				Date *dp = m_datePtrs[pn];
				// if this is NULL now then it got absorbed
				// in this for loop ???
				if ( ! dp ) { char *xx=NULL;*xx=0; }
				// skip if me
				if ( dp == di ) continue;
				// skip if already incorporated
				if ( dp->m_used == di ) continue;

				// no jump backs
				//if( savedLast&&savedLast->contains2(dp->m_a))
				//     continue;


				// if an isolated year, must have tag before
				if ( dp->m_type == DT_YEAR ) {
					// get guy before it
					int32_t pre = dp->m_a - 1;
					// backup over punct
					if (pre>0&&!m_wids[pre]&&!m_tids[pre])
						pre--;
					// skip if alnum word before year,
					// it's probably not a header???
					// NO! breaks antiques thing
					//if(dp->m_a>0 && pre>=0&&m_wids[pre] )
					//	continue;
					// only accept single year headers
					// above our date!
					if ( dp->m_a > di->m_a )
						continue;
				}

				// . do not telescope to date in a menu
				// . fixes http://www.residentadvisor.net/
				//   event.aspx?221238
				//   which is telescoping a "no rentry after 
				//   4am" to an event name in the menu: 
				// "2nd Sunday Nyc wit..". 
				if ( dp->m_section && 
				     (dp->m_section->m_flags & SEC_MENU) &&
				     // fix mercury.intouch-usa.com, we think
				     // the cal header is a menu!!
				     !dp->m_calendarSection ) 
					continue;

				// if we telescope to a date in a vertical
				// list we should stop telescoping, and not
				// allow that telscope either to fix
				// stoart.com
				//if ( dp->m_flags & DF_IN_VERTICAL_LIST )
				//	break;

				uint64_t key = (uint32_t)(PTRTYPE)di;
				// shift up
				key <<= 32LL;
				// need DD now, if there
				if ( DD ) key |= DD->m_ptrHash;

				int32_t ret;
				// do we know the result?
				if ( dp->m_norepeatKey == key && key ) {
					ret = dp->m_norepeatResult;
					if ( ret == -1 ) {char *xx=NULL;*xx=0;}
				}
				else {
				// this should replace the logic i commented
				// out below... except in a subset case!!!
					ret = isCompatible(di,dp,&ht,DD,
							 &hasMultipleHeaders) ;
					/*
					static int32_t dcount = 0;
					// print for debug
					SafeBuf dbuf;
					char *is;
					if ( ret ) is = "IS    ";
					else       is = "IS NOT";
					Date *pd = DD;
					if ( ! pd ) pd = di;
					dbuf.safePrintf("dates: %s compat DD=",
							is);
					pd->printText(&dbuf,m_words,false);
					dbuf.safePrintf(" dp=");
					dp->printText(&dbuf,m_words,false);
					log("%s (dcount=%"INT32")",
					    dbuf.getBufStart(),dcount);
					dcount++;
					*/
					// do not repeat result
					dp->m_norepeatKey    = key;
					dp->m_norepeatResult = ret;
				}
					
				// skip if not compatible
				if ( ret ==  0 ) continue;
				// error? return false with g_errno set
				if ( ret == -1 ) {
					// sanity check
					if ( ! g_errno ) {char *xx=NULL;*xx=0;}
					return false;
				}
				// . fix southgatehouse.com which needs 9pm
				//   to telescope to the same ending date but
				//   with a different middle date
				// . if adding this ptr would create a dup DD 
				//   that we finalized on a previous iteration,
				//   then do not allow it
				uint32_t key2;
				if ( DD ) key2 = (uint32_t)DD->m_ptrHash;
				else      key2 = (uint32_t)(PTRTYPE)di;
				key2 *= 439523;
				key2 ^= (uint32_t)(PTRTYPE)dp;
				// if this would be a date we have already
				// added, then skip it!
				if ( comboTable.isInTable ( &key2 ) ) {
					// count it
					//alreadyAdded++;
					continue;
				}

				// now that headers can be below us, get
				// the distance as an absolute value
				//int32_t dist = di->m_a - dp->m_a;
				// fix "July 19, 2010 [[]] noon - 5:00pm [[]] 
				// Wednesday - Saturday" for woodencow.com
				int32_t dist = lastdi->m_a - dp->m_a;
				// is it negative? i.e. header below us?
				if ( dist < 0 ) {
					// make negatives into positives
					dist *= -1;
					// . prefer headers above us first!
					// . should fix guildcinema mispair.
					dist += 100000;
				}
				
				// pick the one right above us
				if ( bestdp && dist >= bestDist ) continue;
				// got a new best
				bestdp   = dp;
				bestpn   = pn;
				bestDist = dist;
			}

			// keep telescoping up if could not find a good one
			if ( ! bestdp ) {
				// did we previously add a date ptr from pp?
				// if so, we have to be sure to use it rather
				// than ignore it because it could be a crucial
				// constraint to the date telscope. should fix
				// guysndollsllc.com so the tod range
				// under on month/daynum does not telescope
				// out and pair with the other month/daynum,
				// BUT shouldn't acc1/acc2 fix that??? i guess
				// they are brother sections. maybe instead the
				// fix relies on adding better implied sections
				// around such beasties!!
				if ( pp->m_mark == (int32_t)(PTRTYPE)di &&
				     // store hours can always skip over
				     // this section to fix thewoodencow.com
				     ! storeHoursCount &&
				     // if we just added our last date from
				     // this section then we can go on up...
				     //(lastdi == di || lastdi->m_section!=pp))
				     lastAddedSection != pp )
					break;
				// otherwise, telescope up
				pp = pp->m_parent; 
				continue; 
			}

			// otherwise add it
			if ( ! DD ) {
				DD = addDate(DT_TELESCOPE,0,di->m_a,di->m_b,0);
				// return false on error
				if ( ! DD ) {
					return false;
					//i = m_numDatePtrs;
					// mark this -- already in addDate()
					//m_overflowed = true;
					//break;
					//return true;
					//return false;
				}
				// add original
				DD->addPtr ( di , i , this );
				// . and make this guy point to us
				// . TODO: now that one date can have 
				//   multiple telescopes... how to fix???
				if ( ! di->m_telescope ) di->m_telescope = DD;
			}
			// make sure we always grab a date from section "pp"
			// from now on, now that we know it has a date that
			// is compatible with us
			pp->m_mark = (int32_t)(PTRTYPE)di;
			// . do not repeat this guy in another parent sec
			// . no, only the last date in the telescope should
			//   have this set for our MULTIPLE HEADER algo
			bestdp->m_used = di;
			// remember these in case of an "undo"
			lastAddedPtr = bestdp;
			lastHasType  = DD->m_hasType;
			// save this
			lastAddedSection = pp;
			// add that
			DD->addPtr ( bestdp , bestpn , this );

			// . this too?
			// . hmm. not sure. this kills some dates being allowed
			//   to exist of events by themselves JUST because they
			//   were used as a telescope header.
			// . i had uncommented this to fix glsc.org i think
			//   from allowing "1pm, 3pm daily" to be used by 
			//   itself even though it was telescoped to by 
			//   Nov 13-23 or something. but now i used a better
			//   solution of the DF_RANGE_RESTRICTED bit algo
			//   to prevent such things. otherwise, we were hurting
			//   legit events.
			// . actually a better way is to only set 
			//   bestdp->m_telescope if di is a month/day date
			//   then! or such a range or list.
			// . better safe than sorry! if there's a chance
			//   that the tod date might be meant to be only with
			//   this month/daynum date then assume it is, 
			//   otherwise we could report the wrong date!
			// . msichicago.com root page lost its daily hours
			//   because of a "MARCH 18" which telescoped to
			//   them... so now we REQUIRE bestdp to have
			//   a range or a list of daynums, a single daynum
			//   does not cut it because di must have "daily"
			//   (DT_EVERY_DAY) in it.
			datetype_t dit = di->m_hasType;
			// ignore lists or ranges
			//dit &= ~DT_LIST_ANY;
			//dit &= ~DT_RANGE_ANY;
			dit &= ~DT_COMPOUND;
			// same for parent date
			datetype_t pit = bestdp->m_hasType;
			pit &= ~DT_LIST_ANY;
			pit &= ~DT_RANGE_ANY;
			pit &= ~DT_COMPOUND;
			// if pit is not certain format, forget dit then
			if ( pit != ( DT_TOD | DT_EVERY_DAY ) ) dit = 0;
			// then check for "Nov 13 - 23 [[]] 1pm, 3pm daily"
			// as in glsc.org and make sure that "1pm, 3pm daily"
			// can not be used as an event date by itself
			// . actually, no. glsc.org used "daily" only when
			//   there was a range or list of monthdays... so
			//   restrict this logic to such cases
			if ( dit == ( DT_MONTH     |
				      DT_DAYNUM    |
				      DT_RANGE_DAYNUM ) )
				// this prevents DF_EVENT_CANDIDATE from 
				// being set for "bestdp" by itself in
				// Events.cpp.
				bestdp->m_telescope = DD;
			// same goes for list of monthdays
			if ( dit == ( DT_MONTH     |
				      DT_DAYNUM    |
				      DT_LIST_DAYNUM ) )
				bestdp->m_telescope = DD;


			// set this for measuring distance now
			lastdi = bestdp;
			// note it
			//last = bestdp;
			// mark as having been used as a telescope header
			bestdp->m_flags |= DF_USEDASHEADER;
			// accumulate
			//accum |= bestdp->m_hasType;
			// remember the max so we can eliminate int16_ties
			if ( DD->m_numPtrs > maxPtrs )
				maxPtrs = DD->m_numPtrs;
			// stop telescoping to avoid a breach
			if ( DD->m_numPtrs >= 100 ) break;
			// try to get another header from this same section!
			// no! make our section around ptr we added
			pp = bestdp->m_section;
		}
		// keep going if did not telescope
		if ( ! di->m_telescope ) continue;
		// the original might have telescoped, but then we called
		// "goto repeat" to add another telescope that did not
		// work out
		if ( ! DD ) continue;
		// set flag on int16_ties
		//if ( DD->m_numPtrs < maxPtrs )
		//	DD->m_flags |= DF_SUB_DATE;

		// if we hit the max stop the whole telescoping loop
		//if ( m_numDatePtrs >= MAX_DATE_PTRS ) break;

		// ok, make sure that the last header we added was NOT
		// a daynum or daynum range, that is not allowed!!
		// fixes: "Every Monday 7:00p - 10:00p [[]] 6-8, 9-13, 14-18"
		// and other crap like that from
		// http://www.abqfolkfest.org/resources.shtml
		bool remove = false;
		// crap, this removes "8:00 PM [[]] JAN [[]] 28" which is
		// a good thing for reverbnation.com, so let's try commenting
		// this out... no... commenting this out hurt a few more urls
		// because telescoping to a single number daynum is quite 
		// risky... most of the time it is not really a daynum!
		if ( lastAddedPtr && lastAddedPtr->m_type == DT_DAYNUM       )
			remove = true;
		if ( lastAddedPtr && lastAddedPtr->m_type == DT_RANGE_DAYNUM )
			remove = true;
		// are we in the clear?
		if ( ! remove ) {
			// sanity
			if ( DD->m_ptrHash == 0 ) { char *xx=NULL;*xx=0; }
			// add to combo table
			if ( ! comboTable.addKey(&DD->m_ptrHash)) return false;
			// if had multiple headers, try this same di again!
			//if ( ! hasMultipleHeaders ) continue;
			// clear m_used for all but the last
			// . crap, but then southgatehouse.com messes up
			//   because it needs to do
			//   "9pm [[]] Sunday [[]] Friday, Jul 30" and
			//   "9pm [[]] Sunday [[]] Friday, Jul 30" where
			//   "Sunday" is actually a DIFFERENT sunday ptr,
			//   and we end up not being allowed to allow it
			//   to use "Friday, Jul 30" again...  so maybe if
			//   we just record the ptr hash of the final product
			//   and just not allow that again!
			//for ( int32_t c = 1 ; c < DD->m_numPtrs - 1 ; c++ )
			for ( int32_t c = 1 ; c < DD->m_numPtrs ; c++ )
				DD->m_ptrs[c]->m_used = NULL;
			// re-do this same di
			goto repeat;
		}
		// for sanity
		lastAddedPtr = NULL;
		// ok, now unadd it!
		DD->m_numPtrs--;
		// no more daynum
		DD->m_dayNum = -1;
		// revert m_hasType
		DD->m_hasType = lastHasType;
		// fix ptr hash
		DD->m_ptrHash *= 345930;
		// sanity
		if ( DD->m_ptrHash == 0 ) DD->m_ptrHash = 456789;
		// keep going if still telescoped to SOMETHING
		if ( DD->m_numPtrs >= 2 ) goto redo;
		// otherwise un-add it completely
		m_numDatePtrs--;
		// and un-telescope di
		di->m_telescope = NULL;
		// this is gone
		DD = NULL;
		// we set bestdp->m_used = di above, so let's re-do and try
		// to get a header that is really a header, and not some
		// random isolated number
		goto redo;
	}
	// save mem
	ht.reset();

	// set headerCount on all double telescopes
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// must be telescope
		if ( di->m_type != DT_TELESCOPE ) continue;
		// skip if not exactly 2 ptrs...
		//if ( di->m_numPtrs != 2 ) continue;
		// int16_tcut
		int32_t np = di->m_numPtrs;
		// if we do not have this it hurts
		// http://www.reverbnation.com/venue/448772 by making the
		// "8pm" of the first event as the header
		if ( np != 2 ) continue;
		// inc the header count
		di->m_ptrs[np-1]->m_headerCount++;
	}		

	//
	// now set DF_DUP on dates that are dups of others
	//
	HashTableX subdates;
	char sdbuf[10000];
	subdates.set ( 4,4,128,sdbuf , 10000 , false , m_niceness,"subdates");
	HashTableX dt;
	char dtbuf[2000];
	dt.set ( 4 , 4 , 128 , dtbuf , 2000 , true , m_niceness,"dfduptab" );
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// zero out
		di->m_tmph = 0;
		// must be telescope
		//if ( di->m_type != DT_TELESCOPE ) continue;
		// make a hash
		uint32_t h1 = 0;
		uint32_t h2 = 0;
		// sanity check
		if ( di->m_numPtrs == 0 ) { 
			// use the ptr as the base of the hash
			//uint32_t nh = (uint32_t)di;
			uint32_t nh = di->m_arrayNum;
			// always add one to fix 0,20 colliding with 20
			// where those numbers are arraynums
			nh += 1;
			// mix it up some. i even had to add nh here to
			// fix unm.edu collision
			nh = hash32h ( nh , 846657*nh );
			// xor it in
			h1 ^= nh;
			// shift up
			//h <<= 1;
			h2 = h1;
		}
		// count how many we hash
		int32_t numProcessed = 0;
		Date *lastGood = NULL;
		uint32_t nh = 0;
		// hash the ptrs together
		for ( int32_t j = 0 ; j < di->m_numPtrs; j++ ) {
			// int16_tcut
			Date *dj = di->m_ptrs[j];
			// hash for all subdates
			if ( h1 ) subdates.addKey ( &h1 , &di );
			// convert ptr to really random #
			//int32_t nh=hash32h ( (uint32_t)di->m_ptrs[j] ,123);
			//uint32_t nh = (uint32_t)dj;

			// we have to fix a date for dmjuice.com that had
			// "X [[]] Y [[]] Z" so "X [[]] Z" is a subdate.
			// so we have to permute over all the date ptrs
			// in the telescope and hash each permutation of
			// the telescoping pieces. BUT for now just hack
			// it!!! TODO: permute properly for subdate detector.
			if ( di->m_type == DT_TELESCOPE && j == 2 ) {
				// undo last date hash
				uint32_t fh = h1 ^ nh;
				// put ours in
				fh ^= dj->m_tmph;
				// and add that
				subdates.addKey ( &fh , &di );
			}


			// . if already set then use that
			// . like if we have "x [[]] y" then use x's
			//   m_tmph because it will be different
			if ( di->m_type == DT_TELESCOPE ) // dj->m_tmph ) 
				nh = dj->m_tmph;
			else {
				nh = dj->m_arrayNum;
				// always add one to fix 0,20 colliding with 20
				// where those numbers are arraynums
				nh += 1;
				// mix it up some. i even had to add nh here to
				// fix unm.edu collision
				//nh = hash32h ( nh , 3409587+nh );
				nh = hash32h ( nh , 846657*nh );
				// save i guess as hash of this date
				dj->m_tmph = nh;
			}

			// xor it in
			h1 ^= nh;

			// if it is a weak dow and we have a strong, we ignore
			// it in general, like in addIntervals() we will add 
			// it in a hacked way as a single infintitely int32_t 
			// interval if it is weak, because the strong dow 
			// overrides it. this fixes southgatehouse.com which 
			// has "Sunday Valley" as a band name playing on a 
			// friday.
			if ( dj->m_type == DT_DOW &&
			     (dj->m_flags & DF_HAS_WEAK_DOW) &&
			     (di->m_flags & DF_HAS_STRONG_DOW) )
				continue;

			// a secondary hash
			h2 ^= nh;

			// set this
			lastGood = dj;
			// count how many we did
			numProcessed++;

			// shift up -- no, needs to be order independent
			//h <<= 1;
		}
		// if we ignored a weak dow date and ended up having only
		// one non-ignored date ptr, to be deduped properly, we have
		// to set the hash as if we only had one date ptr
		if ( numProcessed == 1 ) {
			// use the ptr as the base of the hash
			//uint32_t nh = (uint32_t)lastGood;
			// mix it up some. i even had to add nh here to
			// fix unm.edu collision
			//nh = hash32h ( nh , 8466587+nh );
			// use directly now
			h2 = lastGood->m_tmph;
			// must be there
			if ( h2 == 0 ) { char *xx=NULL;*xx=0;}
			// and it is not being deduped below because
			// of the headercount logic, which i am not 100% sure
			// i understand! so set it as a dup here.
			//di->m_flags |= DF_DUP;
			// xor it in
			//h ^= nh;
			// shift up
			//h <<= 1;
		}
		// save for checks below
		di->m_tmph = h2;
		// skip if no ptrs
		if ( h2 == 0 ) continue;
		// add to table, returns false with g_errno set on error
		if ( ! dt.addKey ( &h2 , &di ) ) return false;
	}

	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;

		// int16_tcut
		uint32_t key = di->m_tmph;

		// are we a sub-date?
		bool isSubDate = subdates.isInTable ( &key );
		// . if he is ONGOING and we are not, assume he's a refinement
		// . fixes rateclubs.com so that
		//   "Every Tuesday [[]] before 9PM [[]] 7:30PM - 11:30PM"
		//   is not a superdate of
		//   "Every Tuesday [[]] 7:30PM - 11:30PM"
		//   because they are both correct
		// . crap, i guess DF_ONGOING is not what we wanted because
		//   it thinks "May 30 through Aug 22, 2008" on salsapower.com
		//   is ongoing... so for "before 9PM" try
		//   DF_BEFORE/AFTER_TOD flag
		if ( isSubDate ) {
			// get slot
			Date *dx = *(Date **)subdates.getValue ( &key ) ;
			// . who are we a subdate of?
			// . TODO: require date types to match?
			dateflags_t open = (DF_BEFORE_TOD|DF_AFTER_TOD);
			if ( (dx->m_flags & open)  &&
			     !(di->m_flags & open) )
				// turn it off
				isSubDate = false;
		}

		if ( isSubDate ) {
			di->m_flags |= DF_SUB_DATE;
			// get slot
			Date *dx = *(Date **)subdates.getValue ( &key ) ;
			// who are we a subdate of?
			di->m_subdateOf = dx;
		}

		// must be telescope
		if ( di->m_type != DT_TELESCOPE ) continue;

		// see if a dup
		int32_t slot = dt.getSlot ( &key );
		// scan it
		for ( ; slot >= 0 ; slot = dt.getNextSlot(slot,&key) ) {
			// get value
			Date *val = *(Date **)dt.getValueFromSlot(slot);
			// skip if us
			if ( val == di ) continue;
			// sanity check
			if ( val->m_tmph != key ) { char *xx=NULL;*xx=0; }
			// we can have a single date duping with a multi-date
			// if one of the dates in the multi-date was a weak
			// dow and we reverted it to just the hash of one of
			// its multi dates
			Date *vf = val;
			if ( val->m_numPtrs > 0 ) vf = val->m_ptrs[0];
			// . compare otherwise
			// . if you core here then val->m_numPtrs is probably
			//   zero and had a bad hash collision for m_tmph!!
			if ( vf->m_headerCount >
			     di->m_ptrs[0]->m_headerCount )
				continue;
			// are we a dup then?
			if ( vf->m_headerCount <
			     di->m_ptrs[0]->m_headerCount ) {
				di->m_flags |= DF_DUP;
				di->m_dupOf = val;
				break;
			}
			// ok, a tie goes to m_a of ptrs[0]
			if ( vf->m_a < di->m_ptrs[0]->m_a )
				continue;
			// are we the dup?
			if ( vf->m_a > di->m_ptrs[0]->m_a ) {
				di->m_flags |= DF_DUP;
				di->m_dupOf = val;
				break;
			}
			// . these can even tie as seen in southgatehouse.com
			//   "9pm [[]] Sunday [[]] Friday, Jul 30"
			//   "9pm [[]] Friday, Jul 30"
			//   which both have the same m_tmph (deduphash)
			//   because Sunday is a "weak dow" (DF_HAS_WEAK_DOW)
			//   overridden by the DF_STRONG_DOW "Friday"
			if ( val->m_numPtrs < di->m_numPtrs ) {
				di->m_flags |= DF_DUP;
				di->m_dupOf = val;
				break;
			}
			if ( val->m_numPtrs > di->m_numPtrs ) 
				continue;
			// then default to whoever is first
			if ( val < di ) {
				di->m_flags |= DF_DUP;
				di->m_dupOf = val;
				break;
			}
		}
	}



	/*
	// now set DF_UPSIDEDOWN for dates that have a header date as their
	// base date... Events.cpp should avoid these.
	// i put this in to fix newmexico.org date header from telescoping
	// to a tod above. then i thought maybe we should not telescope
	// up to tods, but then "open at 7pm on x y and z" needs to telescope
	// to a tod. maybe we do need to add fake sections then??? 
	// since fake sections would be for Dates.cpp maybe make them
	// look for sections that just have a date and use those as headers?
	// 
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// must be telescope
		if ( di->m_type != DT_TELESCOPE ) continue;
		// skip if only 1 ptr
		if ( di->m_numPtrs != 2 ) continue;
		// check the balance
		if ( di->m_ptrs[0]->m_headerCount >
		     di->m_ptrs[1]->m_headerCount )
			di->m_flags |= DF_UPSIDEDOWN;
	}		
	*/

	// if we have x ... y ... z which results in dates:
	// y [[]] x  and z [[]] y  where x and z are the same date type
	// then we should eliminate one of the pairs that if the other pair
	// has a simple TOD as its base date. i.e.
	// x = 7pm  y = Fri, 29 Jan 2010  z = 10am
	// this fixes problems when we have a list of date headers and
	// regular dates, like for the upcoming events in newmexico.org.
	// the sections in this case are a little different and we might
	// consider relying on that, but really, this solution is more generic
	// and can deal with even if the sections were not distinct, i.e.
	// a list of dates and header dates with no differentiating sections
	// except by the format of the dates themselves.

	// now dedup dates like x [[]] y  and  y [[]] x
	


	//////////////////////////////
	//
	// this table is quite handy! the "TOD" table...
	// although it shold be called the "event date table"
	//
	// Events.cpp uses it to see if an address is owned by another date
	//
	//////////////////////////////	

	int32_t is = m_numDatePtrs * 4;
	// just init it, fast and does not allocate
	if ( ! m_tt.set(4,4,is,NULL,0,true,m_niceness,"todtable") )
		return false;
	// keep counts in this table for use by MULT_DATE penalty in Events.cpp
	if ( ! m_tnt.set(4,4,is,NULL,0,true,m_niceness,"todnumtable") )
		return false;
	// a flag to only init once
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// stop if we hit the telescope section
		if ( di->m_type == DT_TELESCOPE ) break;
		// skip if fuzzy
		if ( di->m_flags & DF_FUZZY ) continue;
		//
		// i moved a lot of the logic i commented out, into Events.cpp
		// itself!
		//
		// or a copyright date
		//if ( di->m_flags & DF_COPYRIGHT ) continue;
		// skip if a "last modified" preceeds it, it is talking
		// about a pub date for the page. last updated, etc.
		//if ( di->m_flags & DF_PUB_DATE ) continue;
		// and ticket/registration dates
		//if ( di->m_flags & DF_NONEVENT_DATE ) continue;
		// skip close dates
		//if ( di->m_flags & DF_CLOSE_DATE ) continue;
		// skip if like (5th & 6th) which is actually between
		// 5th and 6th streets for blackbirdbuvette, that can't
		// claim an address really
		if ( di->m_hasType == DT_DAYNUM ) continue;
		// unm.edu has "closed 1st and 15th of every month" and we
		// need that in this table for setting
		if ( di->m_type == DT_LIST_DAYNUM &&
		     !(di->m_suppFlags & SF_NON_FUZZY) ) continue;
		if ( di->m_hasType == (DT_DAYNUM|DT_RANGE_DAYNUM) ) continue;
		// skip if has no tod
		// . try just any date now!
		// . no i can think of some counterexample to that like
		//   "<div1>some header
		//     <div2>
		//       <div3>crane festiveal mar 6 2009 - apr 3 2009
		//         <div4>114 morningside ave.
		// . and that basically stops the address from telescoping
		//   prematurely
		// . no i had basically an event section with no tod, that
		//   had a month/daynum range (panjea.org) and said to
		//   call for details and it had an "after at" address! so
		//   that after at address was causing all its neighboring 
		//   events that had tods to get SEC_MULT_ADDRESSES
		//if ( ! ( di->m_hasType & DT_TOD ) ) continue;
		// skip if not in body
		if ( di->m_a < 0 ) continue;
		// get section
		Section *sp = m_sections->m_sectionPtrs[di->m_a];
		// telescope up!
		for ( ; sp ; sp = sp->m_parent ) {
			// hash it, return NULL with g_errno set on error
			if ( ! m_tt.addKey ( &sp , &di ) ) return false;
			// counts
			if ( ! m_tnt.addTerm32 ( (int32_t *)&sp, 1)) return false;
		}
	}


	// sanity check
	if ( ! m_nd->m_spideredTimeValid ) { char *xx=NULL;*xx=0; }
	// int16_tcut
	//Sections *ss = m_sections;
	/*
	//////////////// 
	//
	// . add votes for dates being in future/past/current time
	// . then in order to add an event whose date is within 24 hrs of
	//   the current time we must have had other dates in that section
	//   with the SV_FUTURE_DATE set, to be sure it is not a comment date
	//   or a clock date.
	//
	////////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// must have month daynum and year
		//if ( ! ( di->m_hasType & (DT_DAYNUM|DT_MONTH|DT_YEAR) ) )
		//	continue;
		// must have a valid timestamp then!
		if ( ! di->m_timestamp ) continue;
		// the section flag type
		int32_t sectionType = SV_CURRENT_DATE;
		// past, current or future?
		if ( di->m_timestamp < m_nd->m_spideredTime - 24*3600 )
			sectionType = SV_PAST_DATE;
		if ( di->m_timestamp > m_nd->m_spideredTime + 24*3600 )
			sectionType = SV_FUTURE_DATE;
		// . get section that contains this date's first component
		// . might be a telescoped date that touches several sections
		Section *sn = di->m_section;
		// the diff
		int32_t delta = di->m_timestamp - m_nd->m_spideredTime;
		// set the appropriate bit
		if ( ! ss->addVote(sn,sectionType,delta) ) return false;
	}
	*/
	//////////////////////////////
	//
	// set DF_BAD_RECURRING_DOW
	//
	//////////////////////////////

	// this saves us that one bad recurring date in obits.abqjournal.com
	// but it really hurts us in many more ways...
	// this also fixes abqfolkdance.org by not allowing dates like
	// "evening [[]] 8:15" through. that section is really meant as
	// a tod breakdown of the main time range, so we could at some point
	// put in support for that.

	/*
	suppflags_t sfmask = 0;
	sfmask |= SF_FIRST;
	sfmask |= SF_SECOND;
	sfmask |= SF_THIRD;
	sfmask |= SF_FOURTH;
	sfmask |= SF_FIFTH;
	sfmask |= SF_EVERY;
	sfmask |= SF_PLURAL;
	// stop recurring dates like "Tuesday from 3:00 p.m. until 7:30 p.m"
	// in obits.abqjournal.com from being recurring.
	// if we got just a DOW and no daynum, no dow range, and not plural
	// DOW and does not have "each" or "every" before it, then we
	// are invalid, not recurring. set DF_BADDATEFORMAT
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// must match all these
		if ( ! (di->m_hasType & DT_DOW) &&
		     // look for 3pm [[]] evening or christmas [[]] 7pm too
		     ! (di->m_hasType & DT_HOLIDAY) ) continue;
		// if we got like "Monday-Friday" in our telescope, that's ok
		if ( di->m_hasType & DT_RANGE_DOW ) continue;
		// and no range of dows, "each Tuesday" or "Tuesdays" etc.
		if ( di->m_suppFlags & sfmask ) continue;
		// no daynum
		if ( di->m_hasType & DT_DAYNUM ) continue;

		// now, the last chance save. if we telescope up and find that
		// before hitting a section of tods that we get a date that
		// has a DOW range, we are saved! protects store hours

		// get our section
		Section *sp = di->m_section;
		// the telescope loop
	subloop:
		// get first slot in this section
		int32_t slot = m_tt.getSlot ( &sp );
		// count all that are not us
		int32_t count = 0;
		// loop over all tods that telescoped up to this sec.
		for ( ; slot >= 0 ; slot = m_tt.getNextSlot(slot,&sp) ) {
			// get it
			Date *dx = *(Date **)m_tt.getValueFromSlot(slot);
			// skip if us
			if ( dx == di ) continue;
			// count it
			count++;
			// stop if it has a dow range
			if ( dx->m_hasType & DT_RANGE_DOW ) break;
		}
		// if it had a dow range we are saved
		if ( slot >= 0 ) continue;
		// otherwise, if none found, telescope and keep going
		if ( count == 0 ) {
			// telescope
			sp = sp->m_parent;
			// and do some more
			if ( sp ) goto subloop;
		}
		//
		// and mark it so we know what's up
		//
		di->m_flags |= DF_BAD_RECURRING_DOW;
	}


	*/


	//
	// if a date is being used as a header and also as a base for
	// a telescope, keep one and invalidate the other by setting the
	// DF_MULTIUSE and DF_INVALID flag
	//



	///////////////////////////////////////
	//
	// part 9
	//
	// if we only have one year on the page, append that to dates
	// missing the year. ignore copyright years.
	// 
	///////////////////////////////////////

	// NO! this failed on:
	// http://www.zvents.com/albuquerque-nm/events/show/88543421-the-love-song-of-j-robert-oppenheimer-by-carson-kreitzer

	/*
	int32_t year = -1;
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// skip if has no year
		if ( ! ( di->m_hasType & DT_YEAR ) ) continue;
		// ignore copyright years
		if ( di->m_flags & DF_COPYRIGHT ) continue;
		// skip ranges, cuz "2008-2010" range has m_year set to 0
		// since it is not valid
		if ( di->m_type == DT_RANGE && di->m_year <= 0 ) {
			// no assumed year
			year = -2;
			break;
		}
		// skip if invalid (has some disagreement in it)
		if ( di->m_flags & DF_INVALID ) continue;
		// must have a year if DT_YEAR is set
		if ( di->m_year == 0 ) { char *xx=NULL;*xx=0; }
		// . keep going if it already agrees with what we got
		// . di->m_year is 0 if it has none.. how can that happen here?
		if ( year == di->m_year ) continue;
		// to be a candidate it must be bookended!
		//if ( ! ( di->m_flags & DF_LEFT_BOOKEND  ) ) continue;
		//if ( ! ( di->m_flags & DF_RIGHT_BOOKEND ) ) continue;
		if ( di->m_flags & DF_FUZZY ) continue;
		// must have more than just a year
		if ( di->m_type == DT_YEAR ) continue;
		// stop if range of years
		//if ( di->m_minYear != di->m_maxYear ) { year = -2; break; }
		// get it
		if ( year == -1 ) { 
			year = di->m_year; continue; }
		// if in agreement, keep going
		if ( di->m_year == year ) continue;
		// otherwise, we have no unifed year
		year = -2;
		break;
	}
	// add in the assumed year
	for ( int32_t i = 0 ; year > 0 && i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// skip if has year already
		if ( di->m_hasType & DT_YEAR ) continue;
		// or if just a daynum or modifier
		if ( di->m_type == DT_DAYNUM ) continue;
		//if ( di->m_type == DT_MOD    ) continue;
		// add it in
		di->m_year = year;
		// set flag
		//di->m_flags |= DF_HAS_YEAR;
		di->m_hasType |= DT_YEAR;
		// this one too
		di->m_flags |= DF_ASSUMED_YEAR;
	}
	*/

	// . treat "tomorrow" as relative to pub date


	/////////////////////////////////////
	//
	// set Date::m_penalty (for pub date detection)
	//
	/////////////////////////////////////

	// assume no article
	m_firstGood = m_lastGood = -1;

	// . sets them to -1 if no article
	// . otherwise they contain the article between them
	//if ( m_sections ) ss->getArticleRange ( &m_firstGood , &m_lastGood );

	dateflags_t uf = DF_MATCHESURLMONTH|DF_MATCHESURLDAY|DF_MATCHESURLYEAR;

	// . ok, now rank the pub dates in order of most probable to least
	// . the date with the smallest penalty wins
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get flag
		dateflags_t flags = di->m_flags;
		// reset
		di->m_penalty = 0;

		// . NEW STUFF
		// . get his section
		Section *sn = NULL;
		// set section
		if ( di->m_a >= 0 ) sn = m_sections->m_sectionPtrs[di->m_a];
		// use "vote" flags (taken from datedb voters)
		if ( sn ) {
			// big penalty if we are voted as a clock!
			//if ( m_osvt->getScore(sn,SV_CLOCK) > 0.5 )
			//	di->m_penalty += 5000000;
			// . HACK: SV_TEXTY_MAX_SAMPLED is a vote statistic!
			// . if we are a unique section, reward ourselves
			// . this means that at least one vote had 2+ 
			//   occurences of sections with this sn->m_tagHash
			//if(m_osvt->getNumSampled(sn,SV_TEXTY_MAX_SAMPLED)<=1)
			//	di->m_penalty -= 100000;
		}
		// not a range or list
		if(di->m_hasType&(DT_LIST_ANY|DT_RANGE_ANY)){
			di->m_penalty += 5000000;
			continue;
		}
		// needs a year!
		if ( ! ( di->m_hasType & DT_YEAR ) ) {
			di->m_penalty += 5000000;
			continue;
		}
		// and how about a day or timestamp at least
		if ( ! ( di->m_hasType & DT_DAYNUM ) &&
		     ! ( di->m_hasType & DT_TIMESTAMP ) ) {
			di->m_penalty += 5000000;
			continue;
		}
		/*
		// . put in our votes for clock
		// . stores into Sections::m_nsvt which is added to Sectiondb
		//   when XmlDoc calls Sections::hash()
		if ( flags & DF_CLOCK ) {
			if ( ! ss->addVote(sn,SV_CLOCK,1.0) ) return false;
		else 
			if ( ! ss->addVote(sn,SV_CLOCK,0.0) ) return false;
		}
		*/
		// we put in our vote like this now
		// set SEC_* flags for date format types
		if ( ! (flags & DF_AMBIGUOUS) && (flags & DF_MONTH_NUMERIC) ) {
			/*
			// put in our vote for european
			if ( flags & DF_EUROPEAN  ) 
				if ( ! ss->addVote(sn,SV_EURDATEFMT,1.0) )
					return false;
			// put in our vote for american
			if ( flags & DF_AMERICAN  ) 
				if ( ! ss->addVote(sn,SV_EURDATEFMT,0.0) )
					return false;
			*/
			// what did old voters say about this section?
			/*
			if ( sn ) {
				// . ve = probability it is european format
				// . includes the vote we just added above!
				float ve = m_osvt->getScore(sn,SV_EURDATEFMT);
				// disagreement is bad
				if ( (flags & DF_AMERICAN) && ve > 0.5 )
					di->m_penalty += 1000000;
				if ( (flags & DF_EUROPEAN) && ve < 0.5 )
					di->m_penalty += 1000000;
			}
			*/
		}


		// these are golden
		if ( flags & DF_FROM_RSSINLINK      ) di->m_penalty -= 2000000;
		if ( flags & DF_FROM_RSSINLINKLOCAL ) di->m_penalty -= 2000000;

		if ( flags & DF_FROM_URL      ) di->m_penalty -= 1000000;
		if ( flags & DF_FROM_META     ) di->m_penalty -= 3000000;
		// this is the worst
		//if ( flags & DF_INDEX_CLOCK   ) di->m_penalty += 5000000;
		// this could have been set after the indexdb lookup, thus
		// we are being re-called!
		if ( flags & DF_CLOCK         ) di->m_penalty += 5000000;

		//if ( flags & DF_NOYEAR        ) di->m_penalty += 5000000;
		//if ( flags & DF_TOD           ) di->m_penalty += 5000000;

		// this is bad
		if ( flags & DF_FUTURE        ) di->m_penalty += 5000000;
		// a slight penalty for this to break ties
		if ( flags & DF_NOTIMEOFDAY   ) di->m_penalty += 10;
		// bad tag? script marquee, style, select?
		if ( flags & DF_INBADTAG      ) di->m_penalty += 900000;
		// now only do body
		if ( ! (flags & DF_FROM_BODY) ) continue;
		// unique is very good
		if ( flags & DF_UNIQUETAGHASH ) di->m_penalty -= 100000;
		// just as good as the url i guess! should be better if
		// we got a time of day!
		if ( (flags & uf) == uf &&
		     ! ( di->m_hasType & DT_LIST_ANY ) &&
		     ! ( di->m_hasType & DT_RANGE_ANY ) )
			di->m_penalty -= 1000000;

		// if he is american and we are european
		if ( ( flags & DF_AMBIGUOUS ) && ( flags & DF_AMERICAN ) &&
		     m_dateFormat == DF_EUROPEAN )
			di->m_penalty += 5000000;
		// or vice versa
		if ( (flags & DF_AMBIGUOUS) && (flags & DF_EUROPEAN) &&
		     m_dateFormat == DF_AMERICAN )
			di->m_penalty += 5000000;
		// http://www.physorg.com/news148193433.html has a good
		// pub date in a hyperlink!
		//if ( flags & DF_INHYPERLINK )
		//	di->m_penalty += 5000000;
		// if no positive scoring section skip this
		if ( m_firstGood == -1 ) continue;
		if ( m_lastGood  == -1 ) continue;
		// . and the closer to the top part of the positive scoring
		//   section, the better
		// . get our word pos
		// . the delta
		int32_t delta1 = m_firstGood - di->m_a;
		// make positive
		if ( delta1 < 0 ) delta1 *= -1;
		// . same for the bottom of the positive scoring section
		// . that delta
		int32_t delta2 = m_lastGood - di->m_a;
		// make positive
		if ( delta2 < 0 ) delta2 *= -1;
		// make into scores. top is better than bottom a bit
		delta2 *= 2;
		// the bigger these are, the more the penalty
		if ( delta1 <= delta2 ) di->m_penalty += delta1;
		else                    di->m_penalty += delta2;
	}

	// assume no best pub date right now
	m_best = NULL;

	// . get the current best pub date
	// . the best date is the one with the smallest penalty
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// must be a certified known date format
		// set it if its the first
		if ( ! m_best ) { m_best = di ; continue; }
		// skip it if its penalty is higher than the best we got
		if ( di->m_penalty >= m_best->m_penalty ) continue;
		// otherwise, it is the new winner
		m_best = di;
	}

	// if penalty is >= 1M that means definitely NOT a pub date, so if
	// that is all we got, then we don't got nuthin'
	if ( m_best && m_best->m_penalty >= 1000000 ) m_best = NULL;

	// get the winner's flags
	dateflags_t flags = 0; 
	if ( m_best ) flags = m_best->m_flags;

	// in document body? (or url?)
	bool inBody = ( flags & DF_FROM_BODY );


	if ( inBody && ! ( flags & DF_UNIQUETAGHASH ) ) {
		log("date: ignoring repeated tag hash pub dates until "
		    "comment detector is working.");
		m_best = NULL;
	}

	// int16_tcut
	char *u = ""; if ( m_url ) u = m_url->getUrl();

	// or if its ambiguous and not sure sure if american or european
	if (m_best &&inBody && (flags & DF_AMBIGUOUS) && m_dateFormat == -1 ) {
		// set this flag
		m_needQuickRespider = true;
		// note it
		log("date: url %s needs 25 hour ambiguous-based "
		    "respider.", u);
		// and do not take any chances
		m_best = NULL;
	}

	/*
	// cancel out clocks
	if ( m_best && inBody ) {
		// get section
		Section *sn = m_sections->m_sectionPtrs[m_best->m_a];
		// . get the vote that we are a clock
		// . only consult Sections::m_osvt for this one
		float ons = m_osvt->getNumSampled(sn,SV_CLOCK);
		// if he might be a clock but we do nto have any "voting proof"
		// then reschedule for a quick respider 25 hours later
		if ( sn && ! (flags & DF_NOTCLOCK) && ons == 0.0 ) {
			// set this flag
			m_needQuickRespider = true;
			// note it
			log("date: url %s needs 25 hour clock-based respider.",
			    u);
			// and do not take any chances
			m_best = NULL;
		}
	}
	*/

	// set Date::m_maxYearGuess for each date that
	// does not have a year. use the year of the nearest neighbor
	// to determine it. allow for that year minus or plus one if 
	// we also have a DOW. and also allow for that year minus one if
	// we are from a month # greater than our neighbor that supplied
	// the year, assuming he did have a month. so if they have a list
	// like Dec 13th and the neighbor following is Jan 2nd 2011, we 
	// allow the year 2010 for Dec 13th. and only consider non-fuzzy
	// years. so neighbors must be non-fuzzy dates.
	setMaxYearGuesses();


	// . now caller might have determined that the outlink to this page
	//   was added on its parent page between times A and B
	// . fallback to pub date if could not get a min/max from the page
	//if ( m_maxYearOnPage == -1 ) {
	//	if ( minPubDate >= 0 ) y1 = getYear ( minPubDate );
	//	if ( maxPubDate >= 0 ) y2 = getYear ( maxPubDate );
	//}

	// get year
	//int32_t y1 = getYear ( minPubDate );
	//int32_t y2 = getYear ( maxPubDate );
	// if not same, bail!
	//if ( y1 != y2 ) return true;
	// set it
	//int32_t year = y1;
	// ok, now set the assumed year to this for all dates that do
	// not have a year!
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// must agree on year! otherwise, not sure which it could be!
		//if ( y1 != y2 ) break;
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// skip if in a telescope or subdate
		//if ( di->m_subdateOf ) continue;
		// for downtowncorvalis it doesn't set subdateOf when it shuold
		// for September 19th - December 10th
		//if ( di->m_flags & DF_USEDASHEADER ) continue;
		// skip if has year already
		if ( di->m_hasType & DT_YEAR ) continue;
		// or if just a daynum or modifier
		if ( di->m_type == DT_DAYNUM ) continue;
		// do we have a daynum?
		bool hasDaynum = ( di->m_hasType & DT_DAYNUM );
		// fix for signmeup.com : "Thanksgiving [[]] 9:00 am"
		//if ( di->m_suppFlags & SF_NORMAL_HOLIDAY )
		if ( di->m_hasType & DT_HOLIDAY )
			hasDaynum = true;
		// must have a daynum though!
		if ( ! hasDaynum ) continue;
		// skip close dates
		if ( di->m_flags & DF_CLOSE_DATE ) continue;
		// limit to this interval
		int32_t a = m_nd->m_spideredTime;
		// this spidertime is in UTC and the date is not, so the spider
		// time is like 5 hours off or so. so it made an event
		// that started right around the spider time get a 
		// maxStartTime (a different maxStartTime) beyond the 4 hour
		// clock detector limit in Events.cpp that sets EV_SAMEDAY.
		// so EV_SAMEDAY was never getting set.
		a -= 24 * 3600;
		// eveisays.com/jazz.html is 
		// 8 months from when we spidered it
		int32_t b = m_nd->m_spideredTime + DAYLIMIT*24*3600;
		// get max guess year
		int32_t gy = di->m_maxYearGuess;
		// . restrict b some more 
		// . guess year is > 0 if valid
		// . get max timepoint from end of max year from all dates
		//   we checked out on the page
		if ( gy > 0 ) {
			// get end of that year in time_t format
			int32_t ye = getYearMonthStart(gy+1,1) - 1;
			// go out 6 months into that year... if
			// the date has a recurring dow like "ever wednesday"
			// or "every day"
			if ( di->m_suppFlags & SF_RECURRING_DOW )
				// add about 8*30 days to it
				ye += 8*30*86400;
			// restrict b to that
			if ( ye < a ) {
				// forget it if too old
				di->m_flags |= DF_YEAR_UNKNOWN;
				continue;
			}
			// restrict?
			if ( ye < b ) b = ye;
		}
		// . allow some time for new years eve parties
		// . fixes eventvibe.com/...Yacht which was until 1am
		b += 6 * 3600;
		// set the limiting interval
		di->m_minStartFocus = a;
		di->m_maxStartFocus = b;
		// flag it
		di->m_flags |= DF_ASSUMED_YEAR;
	}

	// set the pub date here
	m_pubDate = -1;
	if ( m_best ) m_pubDate = m_best->m_timestamp;

	/////////////////////
	//
	// see if pub date change from last old xml doc to now.
	// that would probably indicate some parsing issues!
	//
	/////////////////////
	if ( m_od && (time_t)m_od->m_pubDate != m_pubDate ) {
		log("build: pub date change since last spider u=%s from "
		    "%"UINT32" to %"UINT32""
		    ,u
		    ,m_od->m_pubDate
		    ,(uint32_t)m_pubDate);
		// ignore it now
		m_pubDate = -1;
		//char *xx=NULL;*xx=0;
	}

	// when this doc was spidered
	if ( ! m_nd->m_spideredTimeValid ) { char *xx=NULL;*xx=0; }

	// no future pub dates allowed
	//int32_t nowGlobal = getTimeGlobal();
	if ( m_best && 
	     m_best->m_timestamp>(time_t)m_nd->m_spideredTime){//nowGlobal){
		// i've seen this happen for
		// http://www.lis.illinois.edu/events/2011/02/18/mix-it-lively-event-community-informatics-seed-fund-recipients-community-partners-
		// because it has the future date in the url...
		log("build: pub date in future utc u=%s !",u);
		// ignore it now
		m_pubDate = -1;
		//char *xx=NULL;*xx=0;
	}
	
	///////////////////////
	// 
	// set SEC_EVENT_BROTHER bits (PART 2)
	//
	// . fix for burtstikilounge.com which does not have recognized dates
	//   until after telescoping
	//
	///////////////////////
	// use this one now that we have telescopes
	setDayXors ();
	// and hopefully we'll find event brothers
	setEventBrotherBits();

	// before when we called it we did not have telescoped dates,
	// so call it again here
	setDateHashes();

	// flag it
	m_setDateHashes = true;

	// need this for printTextNorm()
	setDateParents();

	////////////
	//
	// set DF_IN_LIST if date is in a list
	//
	////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// loop over the date elements of di
		int32_t ne;
		Date **de = getDateElements(di,&ne);
		int32_t dh32 = 0;
		// date elements are sorted by m_a
		for ( int32_t j = 0 ; j < ne ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get it
			Date *dj = de[j];
			// scan up parents
			Date *dp = dj;
			Date *lastdp = NULL;
			for ( ; dp ; dp = dp->m_dateParent ) {
				// is one of our parents a list?
				if ( ! (dp->m_type & DT_LIST_ANY ) ) {
					lastdp = dp;
					continue;
				}
				// yes...
				dj->m_flags |= DF_IN_LIST;
				// but are we the first in the list?
				if ( dp->m_ptrs[0] == lastdp )
					dj->m_flags |= DF_FIRST_IN_LIST;
				// update this
				lastdp = dp;
			}
			//
			// we do have implied sections so set the
			// m_turkTagHash of this date i guess
			//
			// if parent date is a list... skip, but
			// make sure we do get the parent hashed in there
			if (  (dj->m_flags & DF_IN_LIST) &&
			      !(dj->m_flags & DF_FIRST_IN_LIST) )
				continue;
			// preserve the turk tag hash as the dh32 if
			// we only have one date element
			if ( dh32 == 0 ) dh32 = dj->m_turkTagHash;
			// incorporate the tag hash otherwise
			else dh32 = hash32h ( dh32 , dj->m_turkTagHash );
			// however, we also now add in the date type
			dh32 = hash32h ( dh32 , (int32_t)dj->m_type );
		}
		// sanity
		if ( dh32 == 0 ) { char *xx=NULL;*xx=0; }
		// store it
		di->m_dateTypeAndTagHash32 = dh32;
	}


	//////////////////
	//
	// call this a second time to capture telescoped store hours like
	// for bostonmarket.com which has the dow range separated by text
	// from the tod range.
	//
	//////////////////
	setStoreHours ( true );

	//////////////
	//
	// finally set DF_TIGHT if the date is "tight"
	//
	//////////////
	// 
	// . all non-telscoped dates get this set by default
	// . if there's any potential wrongness in the telscoped date we
	//   do not set this...
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// if not telescoped, set and we are done
		if ( di->m_type != DT_TELESCOPE ) {
			di->m_flags |= DF_TIGHT;
			continue;
		}
		// skip this stuff for now. we should probably just
		// make sure that these dates are in their own section
		Section *ds = di->m_ptrs[0]->m_section;
		// compute date's mina and maxb
		int32_t mina = 9999999;
		int32_t maxb = -1;
		for ( int32_t j = 0 ; j < di->m_numPtrs ; j++ ) {
			// get sub date
			Date *sj = di->m_ptrs[j];
			// skip if from url
			if ( sj->m_a < 0 ) continue;
			// test otherwise
			if ( sj->m_a < mina ) mina = sj->m_a;
			if ( sj->m_b > maxb ) maxb = sj->m_b;
		}
		// must have had something not from url
		if ( maxb == -1 ) continue;
		// blow that up until it contains all date ptrs
		for ( ; ds ; ds = ds->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if it contains mina/maxb
			if ( ds->m_a > mina ) continue;
			if ( ds->m_b < maxb ) continue;
			break;
		}
		// how'd this happen?
		if ( ! ds ) continue;
		// ok, now scan all dates in this section. they must all
		// belong to "di" in order for "di" to be tight.
		int32_t fdi = ds->m_firstDate;
		// assume "di" is a tight date
		bool tight = true;
		// scan
		for ( int32_t j = fdi ; j < m_numDatePtrs ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get that date
			Date *dj = m_datePtrs[j];
			// skip if nuked
			if ( ! dj ) continue;
			// stop if outside of "ds" now
			if ( dj->m_a >= ds->m_b ) break;
			// or telescope, stop
			if ( dj->m_type == DT_TELESCOPE ) break;
			// skip if belongs to us
			int32_t k; for ( k = 0 ; k < di->m_numPtrs ; k++ ) {
				// did the date "dj" match one of our
				// date components??? break if so
				if ( di->m_ptrs[k] == dj ) break;
			}
			// if we broke out early then "dj" matched one of
			// the date components of "di"... we are a subdate
			// so we are still tight!
			if ( k < di->m_numPtrs ) continue;
			// if we could not match date "dj" to one of the
			// components in "di" then we are not "tight"
			tight = false;
			break;
		}
		// are we tight?
		if ( tight ) {
			di->m_flags |= DF_TIGHT;
			continue;
		}

		// skip this stuff below for now... kinda too complicated
		/*
		// 1. otherwise expand the section around each date element
		// 2. until right before it hits another element in the date
		// 3. if the section contains date of different type than
		//    the date whose section we are expanding then its
		//    ambiguous and we should not set DF_TIGHT
		int32_t j; for ( j = 0 ; j < di->m_numPtrs ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get it
			Date *dj = di->m_ptrs[j];
			// get section;
			Section *sp = dj->m_section;
			// skip if none
			if ( ! sp ) continue;
			// get initial date bits
			datetype_t idt = sp->m_dateBits;
			// expand it right up until it contains
			// another one of the dates in our list
			Section *ps = sp;
			for ( ; ps ; ps = ps->m_parent ) {
				// scan the other dates
				int32_t k;for ( k = 0 ; k < di->m_numPtrs ; k++ ){
					// breathe
					QUICKPOLL(m_niceness);
					// ge tit
					Date *dk = di->m_ptrs[k];
					// skip if us
					if ( dk == dj ) continue;
					// skip if in url
					if ( dk->m_a < 0 ) continue;
					// stop if all done
					if ( ! ps->m_parent ) break;
					// stop if would contain
					if ( ps->m_parent->contains2(dk->m_a) )
						break;
				}
				// if we not contain any, keep going, otherwise
				// break/stop
				if ( k < di->m_numPtrs ) break;
			}
			// if other dates are in "ps" like other dows that
			// are not in our date then bail!
			// if we've blown up the section around date component
			// "dj" and it has different date types in it, it
			// can not be tight!
			if ( ps->m_dateBits & ~idt ) break;
			// now scan for other dates like us, like DOWs i
			// guess... allow multiple tods if we are a tod,
			// but do not allow a list of other dates, because
			// we might have made a combination error when we
			// build this telscoped date.
			// mdw left off here
			//...

			// ok, now "ps" is the section that contains "dj"
			// and NONE other of the dates in this telescoped
			// dates. so make sure "ps" has no additional date
			// types that could give rise to some "confusion".
			// if this is non-zero then there is extra crap in 
			// there, so do not consider it a tight date
			//if ( ps->m_dateBits & ~idt ) break;
			// otherwise, consider it tight so far
		}
		// if we didn't break out and went all the way, then there
		// was no "confusion".
		if ( j >= di->m_numPtrs ) 
			di->m_flags |= DF_TIGHT;
		*/
	}


	//
	// set DF_INCRAZYTABLE if one of the date elements is in a
	// table that has both a row and col header of dates. like
	// http://www.the-w.org/poolsched.html . this is beyond our
	// parsing ability for now. it has dows in the row header and
	// tods in the first column header.
	//
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		int32_t ne;
		Date **de = getDateElements(di,&ne);
		// so scan each date element then
		int32_t x; for ( x = 0 ; x < ne ; x++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get it
			Date *dx = de[x];
			// get section
			Section *ds = dx->m_section;
			// skip if non
			if ( ! ds ) continue;
			// in crazy table?
			if ( ! ds->m_tableSec ) continue;
			// check it out
			if (!(ds->m_tableSec->m_flags & SEC_HASDATEHEADERROW))
				continue;
			if (!(ds->m_tableSec->m_flags & SEC_HASDATEHEADERCOL))
				continue;
			// ok, it be crazy
			di->m_flags |= DF_INCRAZYTABLE;
			break;
		}
	}


	return true;
}

// set these for printing normalized crap
void Dates::setDateParents ( ) {
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// start it up
		setDateParentsRecursive ( di , NULL );
	}
}

void Dates::setDateParentsRecursive ( Date *di , Date *parent ) {
	// set parent for him
	di->m_dateParent = parent;
	// scan his ptrs
	for ( int32_t j = 0 ; j < di->m_numPtrs ; j++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *dj = di->m_ptrs[j];
		// set em up
		setDateParentsRecursive ( dj , di );
	}
}


// for blackbird buvette "first Thursday" telescopes up to 
// the section right above and grabs the "December 3, 12:30PM" when
// it shouldn't because it has "December 3, 12:00-12:00AM" in its section...
//
// when telescoping date X with date Y, get the biggest section
// around each one, such that they do not intersect, then make sure
// that between those two sections, one does not contain all the
// date types of the other. BUT if no such two sections exist, i.e.
// if one section fully contains the other, then this algorithm does
// not apply. this is a "list of dates" only algorithm. it makes sure
// that elements in a list do not telescope up with each other when
// they should not. it stops list elements of similar date types from
// pairing/telescoping together.
//
// we must allow individual days to telescope to a header row eg.:
// <tr><td>dec 2009</td></tr>
// <tr><td>dec 1</td></tr>
// <tr><td>dec 2</td></tr> ...
//
// "every tuesday" should NOT telescope to "dec 5 2009" here:
// <tr><td>dec 5 2009</td><td>1-3pm</td></tr>
// <tr>beginning every tuesday from dec to january</tr>
// (*"every tuesday" is fuzzy so it can't be a telescope header here.)
// (*a DOW is treated as multiple dayNums for these purposes!)
//
// Often we have a list of table rows and some rows are date headers and
// other rows are non-header but have dates in them.
// for http://www.salsapower.com/cities/us/newmexico.htm we have
// all the table rows as being individual events, and no table rows are
// date headers. yet one event row's day of week, Tuesday, is being
// seens as an event header. 
//
// how to fix this? well let's now require that in order to be a header
// the date should be the only text in the section IF AND ONLY IF both
// di and dp's s1 and s2 respectively equal each other, because they
// are list elements of the same parent container. so basically, if you
// have a list of things, the only way that one of the things can be a
// header is if its section, as taken as an item in that list, contains
// only date words.

// --------------

// . for that panjea.org url it was picking up the phone number from one
//   event as the phone number of another, and since the address had its
//   own phone number, it thought there was a phone number disagreement.
//   so now we stem the telescoping of a phone number to the first section
//   containing a TOD to prevent that from happening again. that is
//   what getPhoneTable() does now.
// . we also do this for other items, and may do it for subfields like "at:"
//   or phrases like "at the" in the future.
// . if we determine that "s1" is the same ~type~ of section as "s2" then
//   this returns false, otherwise it returns true
// . we are looking to pair up two sections because they have different data,
//   but if the sections are "brothers" then we should not do that!

// . the core problem is determining if given two sections, s1 and s2, if they
//   are really the same type of section or if one of them is a "header" 
//   section that is meant to augment the other.
// . merely looking at the tag hash of the largest section element is not
//   sufficient because it is very common to insert header rows into a table 
//   that look very similar to the non-header rows.
// . the approach we take is to check to see if the two sections "own"
//   different items of the same type, which would show them to be the same
//   type of section. 
// . some item examples:
//   inlined addresses or verified addresses
//   unverified streets
//   phone numbers
//   email addresses
//   subsections containing exactly the same text (subfields)
//   prices ($4)
//   TODs (timesofday)

// . the problem is exacerbated when you have a section that has multiple
//   subsections each containing its own TOD (effectively an event) and
//   then outside those subsections you have a phone number. each of the
//   tods technically owns the phone number, but when telescoping out the
//   tod section you do not want to own necessarily any other phone number
//   contained by the telescoped section because it might belong to another
//   tod or list of tods entirely! that is why we have to make a hash table
//   for every item we want to compare, so we have getPhoneTable() etc. and
//   those tables telescope out their items until they hit a TOD section.
//   in that way we can properly contain them and bind them to the TOD or
//   TOD sections they are assocaited with.

// . is "s1" a compatible "header" of "s2" (or vice versa)
// . return -1 on error with g_errno set
// . "di" is the first date in the telescope
int32_t Dates::isCompatible ( Date *di , 
			   Date *dp , 
			   HashTableX *ht ,
			   Date *DDarg ,
			   bool *hasMultipleHeaders ) {

	// we are always looping over the same header candidates... so
	// cache the result!


	// no double daynums! slows us down
	if ( di->m_hasType == dp->m_hasType ) return 0;

	// if you are not ticket registration date and he is, do not use him
	// and vice versa. fixes signmeup.com turkeytrot which has
	// "Thanksgiving" in a paragraph with the word "register" so it thinks
	// it has to do with that.
	if ( (di->m_flags & DF_NONEVENT_DATE)!=(dp->m_flags&DF_NONEVENT_DATE))
		return 0;
	if ( (di->m_flags & DF_REGISTRATION) != (dp->m_flags &DF_REGISTRATION))
		return 0;

	if ( di->m_flags5 & DF5_IGNORE ) 
		return 0;

	Date *DD = DDarg;
	if ( ! DD ) DD = di;

	/*
	if ( di->m_a == 1630 &&
	     dp->m_a == 1476 ) // 4th
		log("hey");

	if ( DD->m_numPtrs == 2 &&
	     DD->m_ptrs[0]->m_a== 1630 &&
	     DD->m_ptrs[1]->m_a== 1476 &&
		dp->m_a>=581 && dp->m_a<=600 )
		log("hey2");
	*/

	// allow "Every Friday [[]] before 9PM [[]] 7:30PM - 11:30PM"
	/*
	  mdw left off here
	if ( ( DD->m_hasType & DT_TOD ) &&
	     ( DD->m_flags   & DF_ONGOING ) &&
	     ( dp->m_hasType & DT_TOD ) &&
	     ( dp->m_hasType & DT_RANGE_TOD ) )
		return 1;
	if ( ( dp->m_hasType & DT_TOD ) &&
	     ( dp->m_flags   & DF_ONGOING ) &&
	     ( DD->m_hasType & DT_TOD ) &&
	     ( DD->m_hasType & DT_RANGE_TOD ) )
		return 1;
	*/

	bool exclude = true;
	// but if its a DOW and matches us then that is ok
	// fixes funkefiredarts.com so we can telescope the Friday to
	// another recurring friday:
	// "June 28 - August 14, 2010 [[]] Friday [[]] 
	//  every 2nd and 4th Friday of the Month [[]] ..."
	if ( dp->m_hasType == DT_DOW &&
	     DD->m_dow >= 1 &&
	     //DD->m_minDow >= dp->m_minDow &&
	     //DD->m_maxDow <= dp->m_maxDow )
	     // if base dow bits are subset of parent...
	     (DD->m_dowBits & dp->m_dowBits) == DD->m_dowBits )
		exclude = false;

	// stop "Sundays at 2 PM [[]] Fridays & Saturdays at 8 PM"
	datetype_t d1 = dp->m_hasType;
	datetype_t d2 = DD->m_hasType;
	// take out dow list bit
	d1 &= ~DT_LIST_DOW;
	d2 &= ~DT_LIST_DOW;
	if ( (d1 & d2) == d1 && exclude )
		return 0;

	// if we have a tod and parent is "afternoons", etc. skip it
	if ( (d2 & DT_TOD) && (dp->m_type == DT_SUBDAY) ) 
		return 0;

	// . special store horus fix for burtstikilounge.com
	//   for "1 [[]] Nov 2009 [[]] 8pm-2am [[]] M-F"
	// . strip the TOD for "until midnight[[]]1[[]]Nov 2009[[]] 8pm..."
	datetype_t base = DD->m_hasType & ~DT_TOD;

	// . stop "December [[]] 2pm" for graffiti.org
	// . just do not let month names by themselves telescope up
	if ( di->m_type == DT_MONTH )
		return 0;

	// if we already heave a weak DOW and dp is a weak DOW, do not allow it
	if ( (DD->m_flags & DF_HAS_WEAK_DOW) &&
	     dp->m_hasType == DT_DOW &&
	     (dp->m_flags & DF_HAS_WEAK_DOW) )
		return 0;

	// try to fix "Friday, 29 January, 2010 [[]] 07:00 PM" in the
	// upcoming events menue of newmexico.org. drat, it hurts
	// stuff like "open at 2:30pm on x y and z".
	//if ( dp->m_type == DT_TOD )
	//	return 0;

	// . think of special dow like "SundayS" or "every sunday" like mon/day
	// . "every sunday" or "first sunday of the month" or "sundays"
	//   is not allowed to telescope to a section that has a 
	//   month/daynum because that would defeat the purpose of 
	//   saying "every sunday". stops "Sundays 9am-9pm [[]] 
	//   August 29th" for guysndollsllc.com. however we should 
	//   allow August 29th to telescope to sundays 9am-9pm if 
	//   we want.
	// . but this makes us miss "Every Friday night [[]] 7 - 10 p.m. [[]]
	//   August 1" for salsapower.com and we
	if ( ( DD->m_suppFlags & SF_RECURRING_DOW ) &&
	     // unless we got something like "Fridays: Dec 11, 18 at 8:00pm"
	     // like santafeplayhouse.org has cause we want that to telescope
	     // to "December 10, 2009 - January 3, 2010" because it has a
	     // year...
	     ! ( DD->m_hasType & DT_DAYNUM ) &&
	     ( dp->m_hasType & DT_MONTH ) &&
	     ( dp->m_hasType & DT_DAYNUM ) &&
	     // allow monthday ranges though to fix salsapower.com
	     ! ( dp->m_hasType & DT_RANGE_MONTHDAY ) &&
	     // really any kind of range at this point so that
	     // we can telescope "Saturdays (10am-5pm)" to 
	     // "Nov 6, 2009 - jan 9, 2010" for flavorpill.com/newyork/...
	     ! ( dp->m_hasType & DT_RANGE ) )
		return 0;

	// a tod or tod range is not allowed to telescope to a single
	// daynum... i guess ever??? because it is in a calendar i guess...
	// UNLESS that daynum is the only potential header...
	if ( dp->m_calendarSection &&
	     dp->m_calendarSection != DD->m_calendarSection )
		return 0;
	// don't allow calendar daynum to telescope to tod outside of
	// the calendar
	if ( DD->m_calendarSection && (dp->m_hasType & DT_TOD) )
		return 0;

	//
	// BEGIN TABLE SCANNING ALGO
	//
	// if both in table, must be in same row/col if the table has
	// a date row OR col header (or both)
	//
	Section *di2 = di->m_section;
	Section *dp2 = dp->m_section;
	// if what we are trying to telescope to is in a subtable, then
	// grow him out until his row/col count is for OUR table
	while ( di2 &&
		dp2 &&
		di2->m_tableSec &&
		dp2->m_tableSec &&
		di2->m_tableSec != dp2->m_tableSec &&
		di2->m_tableSec->contains ( dp2->m_tableSec ) )
		// grow dp2 out
		dp2 = dp2->m_tableSec->m_parent;
	// likewise, repeat for di2 if in a subtable of dp2's table
	while ( dp2 &&
		di2 &&
		dp2->m_tableSec &&
		di2->m_tableSec &&
		dp2->m_tableSec != di2->m_tableSec &&
		dp2->m_tableSec->contains ( di2->m_tableSec ) )
		// grow di2 out
		di2 = di2->m_tableSec->m_parent;
	// now if in same table do our ABOVE scanning
	if ( di2->m_tableSec && 
	     di2->m_tableSec == dp2->m_tableSec &&
	     (di2->m_tableSec->m_flags & SEC_HASDATEHEADERROW) ) {
		// sanity
		if ( ! m_dateBitsValid ) { char *xx=NULL;*xx=0; }
		// useful
		datetype_t myBits = di2->m_dateBits;
		// make di2 into table cell
		for ( ; di2 ; di2 = di2->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( di2->m_tagId == TAG_TD ) break;
			if ( di2->m_tagId == TAG_TH ) break;
		}
		// make dp2 into table cell
		for ( ; dp2 ; dp2 = dp2->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( dp2->m_tagId == TAG_TD ) break;
			if ( dp2->m_tagId == TAG_TH ) break;
		}
		// how'd this happen?
		if ( ! di2 ) return false;
		if ( ! dp2 ) return false;
		// if nothing above, forget it! maybe di is the header!
		if ( ! di2->m_aboveCell ) return false;
		// left scan that cell
		for (Section *above = di2; above; above = above->m_aboveCell) {
			QUICKPOLL(m_niceness);
			// a match?
			if ( above == dp2 ) break;
			// if cell has different date type, not compatible
			if ( above->m_dateBits & ~myBits ) return false;
		}
	}
	//
	// END TABLE SCANNING ALGO
	//



	if ( DD && 
	     base == (DT_DAYNUM|
		      DT_MONTH|
		      DT_YEAR|
		      DT_COMPOUND|
		      DT_TELESCOPE)    &&
	     // must have "hours" or "open" in its section to indicate
	     // it is when the store is open
	     (dp->m_flags & DF_STORE_HOURS) &&
	     dp->m_hasType == (DT_TOD|DT_RANGE_TOD) )
		return 1;

	if ( DD && 
	     base == (DT_DAYNUM|
		      DT_MONTH|
		      DT_YEAR|
		      DT_COMPOUND|
		      DT_TELESCOPE|
		      DT_RANGE_TOD)    &&
	     // must have "hours" or "open" in its section to indicate
	     // it is when the store is open
	     (dp->m_flags & DF_STORE_HOURS) &&
	     dp->m_hasType == (DT_DOW|DT_RANGE_DOW) )
		return 1;

	// store hours should not telescope to kitchen hours
	// for blackbirdbuvette.com
	if ( (DD->m_flags & DF_STORE_HOURS)  &&
	     (dp->m_flags & DF_KITCHEN_HOURS) )
		return 0;
	// and kitchen hours to kitchen hours is bad too i guess
	// for blackbirdbuvette.com
	if ( (DD->m_flags & DF_KITCHEN_HOURS)  &&
	     (dp->m_flags & DF_KITCHEN_HOURS) )
		return 0;

	// stop "9:15 and 11:00 AM [[]] Sunday [[]] Oct 18, 1:15PM" for
	// abqcsl.org
	if ( (DD->m_hasType & DT_LIST_TOD) &&
	    !(dp->m_hasType & DT_LIST_TOD) &&
	     (dp->m_hasType & DT_TOD) )
		return 0;

	// allow "Monday, November 23, 2009" to go to "Mon | Tue | Wed" for
	// mrmovietimes.com (and vice versa)
	//if ( (di->m_hasType & DT_DOW) &&
	//     (dp->m_type == (DT_DOW|DT_LIST_DOW)) )
	//     return 1;

	// stop "9:00am - 5:00pm [[]] February 2010" for the 
	// smithsonianmag.com url where February 2010 occurs
	// below the hours in a different section
	//if ( DD->m_hasType == (DT_TOD|DT_RANGE_TOD) &&
	//     dp->m_hasType == (DT_COMPOUND|DT_MONTH|DT_YEAR) &&
	//     dp->m_a > DD->m_a )
	//	return 0;
	//if ( dp->m_hasType == (DT_TOD|DT_RANGE_TOD) &&
	//     DD->m_hasType == (DT_COMPOUND|DT_MONTH|DT_YEAR) &&
	//     DD->m_a > dp->m_a )
	//	return 0;

	// . this is unneccsary and wrong for burtstikiloung.com
	// . stops "21 [[]] November 2009 [[]] Sat" which prevents us from
	//   then telescoping to the store hours i think
	// . is this hurting "July 19, 2010 [[]] *Sunday* [[]] noon - 5:00pm"
	//   for woodencow.com??? we need to telescope to store hours now
	//if ( (DD->m_hasType & DT_DAYNUM) && dp->m_type == DT_DOW )
	//	return 0;

	// for santafeplayhouse stop 
	// "Dec 10, 17, at 8:00pm [[]] Fridays: Dec 11, 18, Jan 1, at 8:00pm"
	if ( (DD->m_hasType & DT_LIST_DAYNUM) &&
	     (dp->m_hasType & DT_LIST_DAYNUM) )
		return 0;
	// for santafeplayhouse stop 
	// "Dec 10, 17, at 8:00pm [[]] Wednesday: Dec 30 at 8:00pm"
	if ( (DD->m_hasType & DT_LIST_DAYNUM) &&
	     (dp->m_hasType & DT_DAYNUM) &&
	     // allow "December 10, 2009 - January 3, 2010" to be header
	     // for santefeplayhouse.org
	     !(dp->m_hasType & DT_RANGE_ANY) )
		return 0;

	// stop "Thursday, Feb 10 7:30p [[]] 2010-11" for 
	// www.zvents.com/san-jose-ca/events/show/159288785-in-the-mood-a-
	// 1940s-musical-revue
	//if ( (DD->m_hasType & DT_DOW) &&
	//     (DD->m_hasType & DT_DOM) &&
	//     (dp->m_hasType & DT_RANGE_YEAR) )
	//	return 0;

	// for santafeplayhouse stop 
	// "Dec 31, 8:00pm to 12:30am [[]] 
	//  Sundays: Dec 13, 20, 27, Jan 3, at 2:00pm"
	datetype_t simple1 = (DT_MONTH|DT_DAYNUM|DT_TOD|DT_YEAR);
	if ( (DD->m_hasType & simple1) == (DT_MONTH|DT_DAYNUM|DT_TOD) &&
	     (dp->m_hasType & simple1) == (DT_MONTH|DT_DAYNUM|DT_TOD) )
		return 0;

	// stop "1-11 [[]] Sun Noon to 6pm" for collectorsguide.com
	if ( di->m_type == DT_RANGE_DAYNUM )
		return 0;

	// for blackbirbuvette.com stop
	// "Monday thru Friday 5pm - 8 pm [[]] 5th & 6th"
	if ( dp->m_type == DT_LIST_DAYNUM )
		return 0;

	// stop "18th and 19th [[]] Fridays and Saturdays at 8pm" for
	// piratecateradio.com
	if ( di->m_type == DT_LIST_DAYNUM && !(dp->m_hasType & DT_MONTH) )
		return 0;

	// fix "Thanksgiving [[]] Jan & Sept" for collectorsguide.com
	if ( di->m_type == DT_HOLIDAY &&
	     // covers xmas, etc. but not "every day"
	     // MDW di->m_num <= HD_SPECIFIC_HOLIDAY_MAX &&
	     (dp->m_hasType & DT_MONTH ) &&
	     !(dp->m_hasType & DT_TOD) &&
	     !(dp->m_hasType & DT_YEAR) )
		return 0;

	// . a daynum can only use a header date that contains a month!
	// . fixes "1 [[]] Monday - Friday 9:00 am.-3:00 pm" for
	//   unm.edu url
	if ( DD->m_type == DT_DAYNUM && !(dp->m_hasType & DT_MONTH) )
		return 0;

	// stop "December 6, 7:30PM [[]] November 2009"
	if ( DD->m_month >= 1 &&
	     dp->m_month >= 1 && 
	     DD->m_month != dp->m_month ) 
		return 0;

	// stop those daynums in burtstikilounge.com from calling is
	// compatible so much!
	if ( dp->m_dayNum >= 1 && 
	     DD->m_dayNum >= 1 &&
	     dp->m_dayNum != DD->m_dayNum )
		return 0;

	// stop "Monday nights [[]] 8 [[]] 9:30 p.m [[]] 9:30 [[]] 10:30"
	// from salsapower.com
	if ( (DD->m_hasType & DT_TOD) && dp->m_type == DT_TOD )
		return 0;

	// stop "Tuesday Night [[]] 7:30-10:30pm [[]] 2:30-4:30pm" for
	// abqfolkfest.org
	if ( (DD->m_hasType & DT_RANGE_TOD) && 
	     (dp->m_hasType & DT_RANGE_TOD) &&
	     // and dp is NOT adding anything new. this fixes soul power
	     // on publicbroadcasting for "November 23 - 27 on * Monday 4:00PM-12:00AM
	     (DD->m_hasType & dp->m_hasType) == dp->m_hasType )
		return 0;

	// stop "[[]] thanksgiving 2008" but allow "on thanksgiving 2008"
	if ( dp->m_hasType == (DT_HOLIDAY|DT_YEAR|DT_COMPOUND) &&
	     !(dp->m_suppFlags & SF_ON_PRECEEDS) )
		return 0;
	// stop telescoping to "thanksgiving" but allow "on thanksgiving"
	if ( dp->m_hasType == DT_HOLIDAY && 
	     !(dp->m_suppFlags & SF_ON_PRECEEDS) )
		return 0;

	// "1st and 15th of each month [[]] last day of the month" for unm.edu
	// . crap! this kills 
	// "Saturday morning from 10:00 am - noon [[]] March 19, 2005" for
	//  http://www.patpendergrass.com/albnews.html because we think of
	// "morning" as a holiday, so let's fix that
	/*
	if ( (DD->m_hasType & DT_HOLIDAY) && 
	     (dp->m_hasType & DT_DAYNUM ) )
		return 0;
	if ( (dp->m_hasType & DT_HOLIDAY) &&
	     (DD->m_hasType & DT_DAYNUM ) )
		return 0;
	*/

	// TODO:
	// if its a closed date it can't telescope outside of its section
	// or outside of its sentence section!!!

	// . stop "March/April [[]] THURSDAYS, 7:30-8:30pm ..." for panjea.org
	// . month names should only be headers...
	// . no, abqtango.org has "7:30-10:30pm" on "10/4 10/18 ..."
	//if ( di->m_hasType == (DT_MONTH           )) return 0;
	//if ( di->m_hasType == (DT_MONTH|DT_LIST)   ) return 0;
	//if ( di->m_hasType == (DT_MONTH|DT_RANGE)  ) return 0;

	// . a range of DOWs does not like a daynum type
	// . crap, this stops "5 [[]] until Midnight [[]] November 2009"
	//   in burtstikilounge.com from pairing up with the store hours
	//if ( (DD->m_hasType & DT_RANGE_DOW) && di->m_dayNum >= 1 )
	//	return 0;
	// crap, this hurts 
	// www.thewoodencow.com/2010/07/19/a-walk-on-the-wild-side/ from
	// telescoping the store hours to the "September 3" reception date
	if ( (DD->m_hasType & DT_RANGE_DOW) && dp->m_dayNum >= 1 &&
	     // no, no, this hurts "8 am. Mon - Fri [[]] March 15 - Oct. 15"
	     // for unm.edu... so add this exception in i guess
	     ! ( dp->m_hasType & DT_RANGE_MONTHDAY ) &&
	     // so to fix woodencow.com:
	     ! ( DD->m_flags & DF_STORE_HOURS) )
		return 0;

	// stop "Every Monday from 7:45-9 pm [[]] Monday November 23, 2009"
	if ( (DD->m_suppFlags & SF_EVERY) && 
	     (dp->m_hasType & DT_DAYNUM) &&
	     // allow "Every Monday from 7:45-9 pm [[]] Apr 19 - May 20, 2008"
	     !(dp->m_hasType & DT_RANGE_MONTHDAY ) &&
	     // allow "Every Monday from 7:45-9 pm [[]] Apr 19 - 23, 2008"
	     !(dp->m_hasType & DT_RANGE_DAYNUM ) )
		return 0;

	// stop "December 10, 2009 - January 3, 2010 [[]] Dec 10, 17, at 8:00p"
	// from santefeplayhouse, just because doesn't make sense
	if ( di->m_hasType ==(DT_DAYNUM|DT_MONTH|DT_YEAR|DT_RANGE|DT_COMPOUND))
		return 0;

	// . if in the same table, must either be in same row or column
	// . no! because people pad tables up with various cells so a
	//   date might only be in one column when it is intended for all. well
	//   when we see that we will fix it.
	if ( di->m_tableCell &&
	     dp->m_tableCell &&
	     di->m_tableCell->m_tableSec &&
	     di->m_tableCell->m_tableSec == dp->m_tableCell->m_tableSec &&
	     di->m_tableCell->m_colNum != dp->m_tableCell->m_colNum &&
	     di->m_tableCell->m_rowNum != dp->m_tableCell->m_rowNum )
		return 0;

	// stop "Fridays, 7:00 PM [[]] Tuesday, June 02, 2000"
	// http://www.abqfolkfest.org/resources.shtml
	// because it has "Last Updated Tuesday, June 02, 2000"
	// BUT this hurts us on "5:00 PM[[]]Mon[[]]28[[]]Sep" for meetup.com
	//if ( (DD->m_hasType & DT_TOD) &&
	//     (DD->m_hasType & DT_DOW) &&
	//     (dp->m_hasType & DT_DAYNUM) &&
	//     (dp->m_hasType & DT_DOW) )
	//	return 0;

	// stop "9am-5pm [[]] Tue-Sun [[]] September 24, 2007" ETC.
	// for http://www.collectorsguide.com/ab/abmud.html
	// CRAP! this hurts santafeplayhouse's
	// "Saturdays: Dec 12, 19, Jan 2, at 8:00pm" from going to
	// "December 10, 2009 - January 3, 2010"
	//if ( ( dp->m_hasType & DT_DAYNUM) &&
	//     ( dp->m_hasType & DT_MONTH ) &&
	//     ( dp->m_hasType & DT_YEAR  ) &&
	//     ( (DD->m_hasType & DT_DOW    ) ||
	//       (DD->m_hasType & DT_HOLIDAY) ) )
	//	return 0;

	datetype_t specialTypes = 
		DT_HOLIDAY      | // thanksgiving
		DT_SUBDAY       | // mornings
		DT_SUBWEEK      | // weekends
		DT_SUBMONTH     | // last day of month
		DT_EVERY_DAY    | // 7 days a week
		DT_SEASON       | // summers
		DT_ALL_HOLIDAYS ; // "holidays"

	// stop "every day of the week, 9am-5pm [[]] Saturdays and Sundays"
	// for http://www.collectorsguide.com/ab/abmud.html
	if ( (DD->m_hasType & specialTypes ) &&
	     // fix "6:30-7:30pm Weekend[[]]4[[]]April 2011 *M*T*W..."
	     // so Weekend doesn't stop telescope to "April 2011-M-T-W-R-F-S"
	     !(dp->m_hasType & DT_MONTH) &&
	     (dp->m_hasType & DT_DOW) )
		return 0;

	// fix "Sunday, Nov 29 2:00p [[]] In the summer of 2001" for
	// http://events.kgoradio.com/san-francisco-ca/events/show/88047269-san-francisco-symphony-chorus-sings-bachs-christmas-oratorio
	if ( (DD->m_hasType & DT_DAYNUM) &&
	     (dp->m_hasType & DT_SEASON) )
		return 0;

	// stop "Every 3rd Thursday [[]] Every Last Thursday of the Month"
	// for http://www.rateclubs.com/clubs/catch-one_14445.html
	suppflags_t rmask = 
		SF_FIRST|
		SF_LAST|
		SF_SECOND|
		SF_THIRD|
		SF_FOURTH|
		SF_FIFTH;
	if ( (DD->m_suppFlags & rmask) &&
	     (dp->m_suppFlags & rmask) &&
	     (DD->m_suppFlags & rmask) != 
	     (dp->m_suppFlags & rmask) )
		return 0;

	// DF_CLOSE_DATE can never be the basis of a date! can only be
	// headers for a non-close date
	//if ( di->m_flags & DF_CLOSE_DATE )
	//	return 0;

	// . non-close dates can NOT telescope to close dates now
	// . events.cpp should just use the close dates in the events
	//   section! it can subtract them from the intervals of the open
	//   dates.
	if ( ! ( DD->m_flags & DF_CLOSE_DATE ) &&
	     ( dp->m_flags & DF_CLOSE_DATE ) )
		return 0;

	// likewise a close date cannot telescope to a non-close date
	if ( ! ( dp->m_flags & DF_CLOSE_DATE ) &&
	     ( DD->m_flags & DF_CLOSE_DATE ) )
		return 0;


	// stop "Thanksgiving [[]] Christmas"
	if ( di->m_type == DT_HOLIDAY && dp->m_type == DT_HOLIDAY )
		return 0;

	// for http://www.collectorsguide.com/ab/abmud.html prevent
	// "10am [[]] 3-12 [[]] Mondays"
	if ( dp->m_type == DT_RANGE_DAYNUM && (DD->m_hasType & DT_DOW) )
		return 0;

	// . for salsapower.com fix "Tuesdays [[]] 1 [[]] 7 - 8:30 p.m"
	// . no! we should recognize that 1 as part of an address now
	//   that we set addresses part way through setting dates
	// . and also that hurt us on "5:00 PM[[]]Mon[[]]28[[]]Sep" 
	//   for meetup.com
	//if ( ( DD->m_hasType & DT_DOW ) && dp->m_type == DT_DAYNUM )
	//	return 0;

	// . for salsapower.com fix "1 [[]] Tuesdays [[]] 7 - 8:30 p.m"
	// . no fuxs up calendars that need "1 [[]] November 2009"
	//if ( di->m_type == DT_DAYNUM ) 
	//	return 0;

	// . quick empty intersection checks
	// . fix "first Wednesday of each month [[]] Every Sunday before 1pm"
	if ( DD->m_dow  >= 0 && dp->m_dow >= 0 &&
	     DD->m_dow  != dp->m_dow &&
	     // but for southgatehouse.com this thinks the band "Sunday Valley"
	     // is all Sundays and it can't telescope to "Friday, Jul 30" which
	     // is the correct date. thus only apply this constraint if the 
	     // DOW we already have is strong, i.e. meaning it is not in
	     // a potentially incorrect format like "Sunday Valley" which is
	     // considered a weak DOW
	     (DD->m_flags & DF_HAS_STRONG_DOW) )
		return 0;

	// get dp/di section
	//Section *s1 = m_sections->m_sectionPtrs[dp->m_a];
	//Section *s2 = m_sections->m_sectionPtrs[di->m_a];
	Section *s1 = dp->m_section;
	Section *s2 = di->m_section;

	Date *dlast = NULL;
	if ( DD && DD->m_numPtrs >= 2 ) dlast = DD->m_ptrs[DD->m_numPtrs-1];

	// "di" and its corresponding last2 represent the first date in
	// the telscope. to fix "December 2009 [[]] 12/01 [[]] 4pm" for
	// 770kob.com we need to also check out the next to last date ptr
	// if DD is not di and has 2 or more ptrs to it!
	Section *s3 = NULL;
	if ( dlast ) s3 = dlast->m_section;
	// kill s3 if same section as s2
	if ( s3 == s2 ) s3 = NULL;


	// if we have DF_CLOSE_DATE set we are a date when the venue is
	// closed, and we are not allowed to telescope outside out sentence!
	// so blow up until we hit our sentence section.
	// this fixes unm.edu's:
	// "Closed the 1st and 15th of each month; last day of the month 
	//  closes at 2:30 pm." where we were getting
	//  "1st and 15th of each month [[]] 2:30pm"
	if ( (DD->m_flags & DF_CLOSE_DATE) && s1 != s2 ) {
		// telecsope up
		for ( ; s2 ; s2 = s2->m_parent )
			if ( s2->m_flags & SEC_SENTENCE ) break;
		// . if we do not include dp, bail
		// . we can equal it as well
		if ( s1 != s2 && ! s2->contains(s1) ) 
			return 0;
		// reset s2
		s2 = di->m_section;
	}

	// save them
	//Section *orig1 = s1;
	//Section *orig2 = s2;

	// if they initially contain each other, then they are compatible
	//if ( s1->contains ( s2 ) ) return 1;
	//if ( s2->contains ( s1 ) ) return 1;

	/*
	// loop over all text sections owned 
	for ( int32_t k = last1->m_a ; k < last1->m_b ; k++ ) {
		// need a tid
		if ( ! m_tids[k] ) continue;
		// get its smallest containing section
		Section *sp = m_sections->m_sectionPtrs[k];
		// get its content hash
		int32_t ch = sp->m_contentHash;
		// hash that
		if ( ! ct.addKey ( 
		// skip if in date
		if ( m_bits[k] & D_IS_IN_DATE ) continue;
		// otherwise, that's bad, we can't be a header
		return 0;
	}
	*/


	// . fix for http://www.salsapower.com/cities/us/newmexico.htm 
	// . if last1 and last2 are brothers in the same list
	// . crap, but we got "<br>monday nights<br>7pm" so we have to
	//   be able to telescope from one item to another in the list
	/*
	if ( s1 == s2 && 
	     // must not be in same item in same container
	     last1 != last2 &&
	     // and the same tagHash (section type)
	     last1->m_tagHash == last2->m_tagHash ) {
		// now if both have an email, phone number or
		// place or other items that might indicate they are
		// describing different things, then "dp" cannot be a header
		// date.
		//if ( last->m_numPlaces > 0 ) return 0;
		//if ( last->m_numAddresses > 0 ) return 0;
		// then basically they are like rows in the same table
		// so for s1 to be a header it must contain only date text
		// in its section, otherwise we might be mistaking it
		// for another event or something!
		// so scan for words not in a date
		for ( int32_t k = last1->m_a ; k < last1->m_b ; k++ ) {
			// skip if in date
			if ( m_bits[k] & D_IS_IN_DATE ) continue;
			// otherwise, that's bad, we can't be a header
			return 0;
		}
	}
	*/

	bool subRange = false;
	/*
	if ( ( dp->m_hasType & DT_RANGE_TOD ) &&
	     ( di->m_hasType & DT_RANGE_TOD ) &&
	     di->m_minTod >= dp->m_minTod &&
	     // folkmads.org potluck is actually after the event tod range!
	     // aw, that sux... even if we allow it to be after it still
	     // gets empty times when doing the intersection. so we'd have
	     // to change that as well.
	     di->m_maxTod <= dp->m_maxTod )
		subRange = true;
	*/

	// you can not put two different tod ranges together... strange
	// ** no, might have a subrange of tods
	if ( ( dp->m_hasType & DT_RANGE_TOD ) &&
	     ( di->m_hasType & DT_RANGE_TOD ) &&
	     // it's ok if subrange though i guess
	     ! subRange &&
	     // allow "Every Sunday before 1pm" to telescope to "Tue-Sun 9-5"
	     // for http://www.collectorsguide.com/ab/abmud.html
	     ! ( di->m_flags & DF_ONGOING ) &&
	     ! ( dp->m_flags & DF_ONGOING ) )
		return 0;

	// allow store hours headers all the time
	/*
	// use his telescope if he got one
	// fixes "8pm-2am [[]]Mon-Sat" for
	// burtstikilounge.com url
	Date *te = dp;
	if  ( te->m_telescope ) te = te->m_telescope;
	// get it
	datetype_t hdt = te->m_hasType;
	// mask it out
	hdt &=   DT_DOW |
		DT_TOD |
		DT_DAYNUM |
		DT_MONTH  |
		DT_YEAR   |
		DT_RANGE_DOW |
		DT_RANGE_TOD ;
	// . if we have a dow range, do not do this
	// . fixes blackbirdbuvette.com kitchen hours mixing with store hours
	if ( DD->m_hasType & DT_RANGE_DOW ) hdt = 0;
	// . or if base date has a single DOW and a tod range
	// . fixes "Every Tuesday 7-9pm [[]] Fri. + Sat. until Midnight" for
	//   blackbirdbuvette.com
	// . TODO: make an exception to this later for half open tod range
	//   intervals like "after 11am" or "until 2pm", those will need to
	//   pair up with store hours
	if ( DD->m_hasType & (DT_RANGE_TOD|DT_DOW) == (DT_RANGE_TOD|DT_DOW) )
		hdt = 0;
	// skip if date is store hours
	if ( hdt == 
	     ( DT_DOW       |
	       DT_TOD       |
	       DT_RANGE_DOW |
	       DT_RANGE_TOD ) ) 
		return 1;
	// or if just a single dow and tod
	// range, that is ok, but watch out
	if ( hdt ==
	     ( DT_DOW       |
	       DT_TOD       |
	       DT_RANGE_TOD ) ) 
		return 1;
	*/

	bool check = true;

	// STORE HOURS header exception.
	// allow "Every Sunday before 1pm" to telescope to "Tue-Sun 9-5" for
	// http://www.collectorsguide.com/ab/abmud.html
	// well, not quite we get 
	// "6 am. Mon. - Sat. [[]] Mon. - Fri. 8 am. - 2 pm." for
	// http://www.unm.edu/~willow/homeless/services.html if we do this,
	// because it is basically a list of store hours!!!
	// so just set check to false i guess...
	// really we need to identify store hours as being in a separate
	// unique section i guess... at least unique relative to telescoping
	// di up, incase we have a list of different stores' hours...
	if ( ( dp->m_hasType & DT_RANGE_TOD ) &&
	     ( dp->m_hasType & DT_RANGE_DOW ) )
		// this was returning true, but it caused unm.edu which
		// is basically a list of headers to telescope to headers
		// in different sections in that list of headers
		check = false;

	// but if di already has a tod range we do not need store hours
	if ( (DD->m_hasType & DT_RANGE_TOD ) )
		check = true;

	// if intersection is empty, obviously can not be header!!
	// fix Saturday 9:30-4pm [[]] Mon-Fri 9:30-5pm for
	// http://www.collectorsguide.com/ab/abmud.html


	// you can mix types if one is closed!
	//if ( dp->m_flags & DF_CLOSE_DATE ) 
	//	check = false;

	// allow "8:30 to midnight" to team up with
	// "Wednesdays at 7:00 p.m" for salsapower.com because its
	// tod fits in dp's tod range.
	// actually do not try to fit TOD ranges because of things like:
	// " Dance Sunday 4:30 to 5:45 p.m. Another intermediate class 
	//   at 6 p.m. too"
	if ( ( di->m_type == DT_RANGE_TOD || di->m_type == DT_TOD ) &&
	     (dp->m_hasType & DT_DOW) &&
	     (dp->m_hasType & DT_TOD) )
		check = false;

	// . if you are telescoping to kitchen hours, check it!
	// . fixes "Sun. - Thur. until 10pm [[]] Monday thru Friday 5pm - 8 pm"
	//   which is kitchen hours telescoping to happy hour
	//   for blackbirdbuvette.com
	if ( dp->m_flags & DF_KITCHEN_HOURS )
		check = true;

	// mask out range, list, composite
	datetype_t hasType = dp->m_hasType;

	// so that "2:00 pm" would telescope to "last day of the month" for 
	// unm.edu url. treat it like a daynum.
	if ( dp->m_type == DT_SUBMONTH && dp->m_num  == HD_MONTH_LAST_DAY )
		hasType = DT_DAYNUM;
	if ( dp->m_type == DT_SUBMONTH && dp->m_num  == HD_MONTH_FIRST_DAY )
		hasType = DT_DAYNUM;

	// now that we are super symmetric...
	datetype_t ditype3 = di->m_hasType;
	if ( di->m_type == DT_SUBMONTH && di->m_num  == HD_MONTH_LAST_DAY )
		ditype3 = DT_DAYNUM;
	if ( di->m_type == DT_SUBMONTH && di->m_num  == HD_MONTH_FIRST_DAY )
		ditype3 = DT_DAYNUM;

	// but if its a DOW and matches us then that is ok
	if ( ! exclude )
		check = false;

	// add DT_DOW for "every Wednesday from 
	// 3:00pm to 4:00 pm, starting 
	// September 9 and continuing through 
	// December 9."
	// so that "every Wednesday" is telescoped to
	// for http://www.dailylobo.com/calendar/
	// . add DT_LIST_DOW so for mrmovietimes.com "Monday, Nov 23, 2009"
	//   would telescope to "Mon | Tue | Wed ..." and we can dedup that
	//   date format with "11:15am, 2:00, 4:50, 7:45, 10:30 [[]] 
	//   Mon | Tue | Wed | Thu | Fri | Sat | Sun [[]] 
	//   Monday, November 23, 2009" which does include the dow menu/list
	hasType &= DT_DOW|DT_MONTH|DT_DAYNUM|DT_YEAR|DT_TOD|DT_LIST_DOW;
	// all the date types we've accumulated so far
	datetype_t accum = 0;
	if ( DD ) accum = DD->m_hasType;
	// or in what we got
	accum |= ditype3; // di->m_hasType;
	// mask that out
	accum &= hasType;

	// do not check if DD has all dp's date types, except dp has
	// DF_ONGOING set! allows:
	// "Every 3rd Thursday [[]]before 9PM" to telescope to "7:30PM-11:30PM"
	// for http://www.rateclubs.com/clubs/catch-one_14445.html
	if ( dp->m_flags & DF_ONGOING ) check = false;
	if ( DD->m_flags & DF_ONGOING ) check = false;

	// do not telescope if the header has all our date types, with the
	// store hours exception above which we return 1 for
	// MDW: added "hasType != 0" because we had a single "halloween" for
	// the "dp" which made hasType 0... this should fix
	// http://www.wpd4fun.org/Events/Halloween.htm
	if ( hasType != 0 && accum == hasType && check )
		return 0;

	int32_t ret = isCompatible2 ( s1 , s2 , true );
	// return -1 on error with g_errno set
	if ( ret == -1 ) { 
		if ( ! g_errno ) { char *xx=NULL;*xx=0;}
		return -1;
	}
	if ( ret ==  0 ) return  0;

	// crap! problem with this algo is on panjea.org we have
	// "shona language class 8:30-9pm" that needs to telescope to
	// the TUESDAYS in the section above it, but the section above it
	// also contains a TOD, so we need to stop this...
	//return 1; -- make exception for br sect w/ same parent!!
	if ( s1->m_parent == s2->m_parent && 
	     to_lower_a(m_wptrs[s1->m_a][1])=='b' &&
	     to_lower_a(m_wptrs[s1->m_a][2])=='r' &&
	     s1->m_tagHash == s2->m_tagHash )
		return 1;

	// fix "Friday, Feb. 11 at 7:30 p.m." going to "2/2/2011 - 2/09/2011"
	// which would make it empty times for http://www.denver.org/events/2fo
	// r1tix?utm_source=onsite&utm_medium=rightrail&utm_campaign=D&D_2for1
	if ( DD->m_dayNum > 0 &&
	     (dp->m_hasType & DT_RANGE) &&
	     (DD->m_hasType & DT_MONTH) &&
	     (DD->m_hasType & DT_MONTH) &&
	     di->m_month == dp->m_month &&
	     dp->m_maxDayNum < DD->m_dayNum )
		return 0;

	//
	// if dates are in the same section do not do this logic below!
	//
	if ( dp->m_section == di->m_section ) return 1;

	// or if dates are both in <br> sections and have same parents
	// likewise, do not do this logic below. this should allow
	// "8:30 to midnight" to telescope to "Wednesdays at 7:00 p.m."
	// in salsapower.com, because they are in different br sections,
	// and actually Wednesdays is in a font subsection! so maybe we
	// need to ignore font,b,br,i sections and telescope out until we
	// hit a section that is not one of those! yeah!!!
	// -- we could extend this to <p> tags too to fix abqcsl.org --
	// -- but what would that break??? --
	// . crap, but this is allowing "Dec 31, 8:00pm to 12:30am" to
	//   telescope to "Thursday" which is in a section that already has
	//   "Dec 10, 17, at 8:00pm" for santafeplayhouse.org
	// . well, let's keep this rule then but set a flag, "inSame"
	//   and mask out all but a month and daynum then. that way if
	//   our biggest section has a month and daynum and so does his, then
	//   the dates are not compatible... that should fix 
	//   santafeplayhouse.org and at the same time let the tods continue
	//   to pair up with each other where applicable.
	bool inSame =  ( dp->m_hardSection == di->m_hardSection );
	//if ( dp->m_hardSection == di->m_hardSection ) return 1;

	/*
	  this is not worth it to just get romy keegan on panjea.org to
	  use the storehours/tod header in the brother section above it
	// . s1 or s2 is the smallest section that contains both dp and di
	// . this should ultimately replace the acc1/acc2 algorithm
	// . the idea is is that if two or more headers of consisting of
	//   the same basic date types (dow/tod/month/daynum/year) exist
	//   in s1/s2 then there is some ambguity and we do not pick either!
	// . NO! messes up guildcinema which has multiple headers for the
	//   date header but you are supposed to pick the one right above you.
	// . also messes up facebook.com and we lose the one event we had
	//   because for the same reason i guess
	if ( ! s1 ) { char *xx=NULL;*xx=0; }
	// only do this for dp being store hours
	if ( storeHours ) {
		// basic type mask
		datetype_t mask1;
		mask1=DT_DOW|DT_MONTH|DT_DAYNUM|DT_YEAR|DT_TOD|DT_RANGE_ANY;
		// get a header date in the s1 section
		int32_t slot = ht->getSlot(&s1);
		// loop over all dates that telescoped up to that section
		for ( ; slot >= 0 ; slot = ht->getNextSlot(slot,&s1 ) ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get it
			int32_t pn = *(int32_t *)ht->getValueFromSlot(slot);
			// get the date index
			Date *hd = m_datePtrs[pn];
			// skip if us
			if ( hd == di ) continue;
			if ( hd == dp ) continue;
			// if its a dup then break, and return 0
			if ( (hd->m_hasType & mask1)==(dp->m_hasType & mask1) )
				break;
		}
		// if we had another date in s1 with the same basic date types
		// as hd then let's stop!
		if ( slot >= 0 ) return 0;
		// otherwise, it's a unique store hours header
		return 1;
	}
	*/
	
	// double crap, texas drums has stuff like:
	// "TUESDAYS: 5:30-7pm" in one row
	// and "7:30-9pm" in the next row and it is supposed to refer
	// to TUESDAYS...
	// maybe try another approach?
	//return 1;


	Section *last1 = s1;
	Section *last2 = s2;
	Section *last3 = s3;

	// blow up "s1" until we hit last section that does NOT contain "s2"
	for ( ; s1 ; s1 = s1->m_parent ) {
		// breathe
		QUICKPOLL(m_niceness);
		// if s1 contains s2 then stop
		if ( s1->contains ( di->m_section ) ) break;
		// . stop if contains a piece of our telescope too
		// . fixes July ... [[]] noon - 5:00pm [[]] * Sunday *"
		//   for woodencow.com otherwise acc3 & acc1 == acc3 because
		//   acc1 grows to contain acc3 unless we stop it here
		if ( s3 && s1->contains ( s3 ) ) break;
		// assign this if doesn't contain it yet
		last1 = s1;
		// . do not telescope to date in a menu
		// . fixes http://www.residentadvisor.net/event.aspx?221238
		//   which is telescoping a "no rentry after 4am" to an
		//   event name in the menu: "2nd Sunday Nyc wit..". 
		//if ( s1->m_flags & SEC_MENU ) 
		//	return false;
	}
	// blow up "s2" until we hit last section that does NOT contain "s1"
	for ( ; s2 ; s2 = s2->m_parent ) {
		// breathe
		QUICKPOLL(m_niceness);
		// if s2 contains header date then stop
		if ( s2->contains ( dp->m_section ) ) break;
		// assign this if doesn't contain it yet
		last2 = s2;
		// . do not telescope FROM date in a menu
		// . fixes http://www.residentadvisor.net/event.aspx?221238
		//   which is telescoping a "no rentry after 4am" to an
		//   event name in the menu: "2nd Sunday Nyc wit..". 
		//if ( s2->m_flags & SEC_MENU ) 
		//	return false;
	}
	// blow up "s3" until we hit last section that does NOT contain "s1"
	for ( ; s3 ; s3 = s3->m_parent ) {
		// breathe
		QUICKPOLL(m_niceness);
		// if s3 contains header date then stop
		if ( s3->contains ( dp->m_section ) ) break;
		// assign this if doesn't contain it yet
		last3 = s3;
	}

	// if you are brothers, check for SEC_TOD_EVENT bit
	//if ( last1->m_parent == last2->m_parent && 
	//     // do not allow telescoping into a brother if we are basically
	//     // a list of events
	//     (last1->m_flags & SEC_TOD_EVENT) &&
	//     // allow brothers that are headings though
	//     !(last1->m_flags & SEC_HEADING_CONTAINER) &&
	//     // or would have been heading containers
	//     !(last1->m_flags & SEC_NIXED_HEADING_CONTAINER) )
	//	return 0;

	// if one date's section contains the other
	if ( last1->contains(last2) ) return 1;
	if ( last2->contains(last1) ) return 1;

	//
	// . fix "Monday through Friday, 8 a.m.-4:30 p.m [[]] The Fall [[]] "
	//        Fall 2011" for www.advising.ufl.edu
	// . do not telescope to seasons unless they are clear headers for us
	//
	// 1. season header must be in same sentence as di
	if ( ( dp->m_hasType == (DT_SEASON|DT_YEAR|DT_COMPOUND) ||
	       dp->m_hasType == (DT_SEASON) ) &&
	     last1 != last2 ) 
		return false;


	// this allowed "July 19, 2010 [[]] noon - 5:00pm" to telescope
	// further to "Wednesday - Saturday" which it shouldn't have, it
	// because it already went to Sunday..
	//if ( last3 && last3->contains(last1) ) return 1;

	/*
	// . try to fix www.woodencow.com
	// . the times in its hour section where telscoping to 
	//   "July 19, 2010" in the list of dates below it.
	// . there is nothing too wrong with that if 1) each store hour
	//   date telescoped to each "calls for art" monthday date
	//   and 2) each monthday date telscoped to each hours date.
	// . but for now rather than do multiple headers we just return
	//   false... TODO: fix

	// . telescope header section thdr until it has a brother
	//   that has the same date types in it
	// . if thdr has such a brother and they are in a container that
	//   does not contain di->m_section then do not telescope to them
	//   because they are probably not related to s2
	// . and s2 must be outside this list
	// . so the dates in the list can't be header dates then
	// . WELL WE CAN indeed telescope to them, but we must telescope
	//   to EACH ONE separately... i.e. MULTIPLE HEADER ALGO
	datetype_t dmask = DT_RANGE_TOD | DT_RANGE_DOW;
	// get the list/container of headers, if any
	Section *thdr = dp->m_section;
	// telescope until hits header brother section, if any...
	for ( ; thdr ; thdr = thdr->m_parent ) {
		// breathe
		QUICKPOLL(m_niceness);
		// stop if contains di section already
		if ( thdr->contains ( di->m_section ) ) break;
		// check brothers
		Section *bro = thdr->m_prevBrother;
		// get header dates in there
		int32_t slot ;
		// come up here for "next" bro as well
	subloop:
		// get it
		if ( bro ) slot = ht->getSlot(&bro);
		else       slot = -1;
		// scan dates to see if one matches us
		for (; slot >= 0 ; slot = ht->getNextSlot(slot,&bro) ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get it
			int32_t pn = *(int32_t *)ht->getValueFromSlot(slot);
			// get the date index
			Date *dx = m_datePtrs[pn];
			// sanity check
			if ( dx == dp ) { char *xx=NULL;*xx=0; }
			// mask our DT_RANGE_TOD and DT_RANGE_DOW
			// same type as us? then break
			if ( (dx->m_hasType & ~dmask) == 
			     (dp->m_hasType & ~dmask) ) break;
		}
		// try next if we need too
		if ( slot < 0 && bro != thdr->m_nextBrother ) {
			bro = thdr->m_nextBrother;
			goto subloop;
		}
		// telescope up more if no hits
		if ( slot < 0 ) continue;
		// set this
		*hasMultipleHeaders = true;
		// ok, we got a container of headers
		//return 0;
	}
	*/

	//int32_t slot = -1;

	/*
	// now loop over all dates in each section and see what types we got
	if ( thdr ) slot = ht->getSlot ( &thdr ); // last1 );
	// loop over all dates that telescoped up to this sec.
	for ( ; slot >= 0 ; slot = ht->getNextSlot(slot,&thdr)) { //last1) ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		int32_t pn = *(int32_t *)ht->getValueFromSlot(slot);
		// get the date index
		Date *dx = m_datePtrs[pn];
		// skip if our guy
		if ( dx == dp ) continue;
		// skip if our section
		if ( dx->m_section == dp->m_section ) continue;
		// get the type
		dateflags_t ht = dx->m_hasType;
		// take out telescope and ranges
		ht &= ~(DT_TELESCOPE|DT_RANGE_TOD|DT_RANGE_DOW);
		// make exception if its a list of tods/dows because
		// those could be store hours!!
		// see www.thewoodencow.com
		// which has two lists, one is store hours, the others is
		// days of the month.
		if ( dx->m_hasType & DT_TOD                 ) continue;
		// for "July 19, 2010 [[]] Wednesday - Saturday [[]] 10am-6pm"
		// for www.thewoodencow.com
		if (dx->m_hasType == (DT_DOW)        ) continue;
		if (dx->m_hasType == (DT_DOW|DT_TOD) ) continue;
		// otherwise, its a brother section, so we are a list
		// separate from last2
		return 0;
	}
	*/

	//	----> break Dates.cpp:7710 if DD->m_numPtrs==2 && DD->m_ptrs[0]->m_a==1978 && dp->m_a==1574

	/*
	// convert a holiday like thanksgiving to a month and daynum
	datetype_t ditype2 = di->m_hasType;
	//if ( di->m_type==DT_HOLIDAY && (di->m_suppFlags&SF_NORMAL_HOLIDAY)){
	// MDW: will this be good enough now that holiday is actually a holiday
	if ( di->m_type & specialTypes ) { // di->m_type == DT_HOLIDAY
		ditype2 &= ~specialTypes; // ~DT_HOLIDAY;
		ditype2 |= DT_MONTH|DT_DAYNUM;
	}

	// furthermore we have to fix southgatehouse.com which has a band
	// named "Sunday Valley" playing on a friday, and it was unable
	// to telescope to that "Friday" because of the constraint below
	// that since ditype (the last date ptr in DD) was "Sunday" and
	// "Friday" was in the date header we were trying to telescope to,
	// we ended up returning false, so, to fix that, ignore weak DOWs
	// for this purpose
	if ( (di->m_flags & DF_HAS_WEAK_DOW) &&
	     !(di->m_flags & DF_HAS_STRONG_DOW) )
		ditype2 &= ~DT_DOW;
	*/

	// if last1 and last2 have the same tag hash then the header date
	// must be ABOVE the base date. this doesn't fix anything i know of
	// but it was originally intended for abqtango.org
	//if( last1->m_tagHash == last2->m_tagHash && last1->m_a > last2->m_a )
	//	return 0;

	dateflags_t acc1 = last1->m_dateBits;
	dateflags_t acc2 = last2->m_dateBits;
	dateflags_t acc3 = 0; if ( last3 ) acc3 = last3->m_dateBits;

	/*
	slot = -1;
	// now loop over all dates in each section and see what types we got
	if ( last1 ) slot = ht->getSlot ( &last1 );
	// reset
	dateflags_t acc1 = 0;
	// loop over all dates that telescoped up to this sec.
	for ( ; slot >= 0 ; slot = ht->getNextSlot(slot,&last1) ) {
		// get it
		int32_t pn = *(int32_t *)ht->getValueFromSlot(slot);
		// get the date index
		Date *dx = m_datePtrs[pn];
		// get its type
		datetype_t dxtype = dx->m_hasType ;
		// convert a holiday like thanksgiving to a month and daynum
		if ( dxtype & DT_HOLIDAY ) {
			// take it out
			dxtype &= ~DT_HOLIDAY;
			// put something else in its place maybe
			if ( dx->m_suppFlags & SF_NORMAL_HOLIDAY ) {
				//dxtype |= DT_MONTH|DT_DAYNUM;
				dxtype |= DT_DAYNUM;
			}
			// everything except generic "holiday" word
			else if ( dx->m_num != HD_HOLIDAYS ) {
				dxtype |= DT_DAYNUM;
			}
		}
		// . if a date in the header date's blown up section contains
		//   a date, "dx", which has all the same date components as
		//   "di" then forget it... "dp" can't be a header for "di" 
		// . ultimately for st-margarets.com i put this in here to fix 
		//   "thanksgiving" telescoping to a tod or tod range in
		//   another section which also contained a month/daynum pair
		//   but the thanksgiving's section contained additional
		//   date components that the header section did not and
		//   so the "acc1" algo below was not working since neither
		//   acc1 or acc2 was a proper subset of the other, BUT by
		//   looking at the individual dates in the header's section
		//   to see if they are similar to "di" we can be even more
		//   accurate
		// . excpetion: do not do this if inSame is true
		if ( (dxtype & ditype2) == ditype2 && ! inSame ) 
			return 0;
		// skip if isolated daynum - they are noisy and often wrong
		// . no, this stops "JAN [[]] 8:00PM" from being
		//   "JAN [[]] 28 [[]] 8:00PM" for
		//   http://www.reverbnation.com/venue/448772
		//if ( dx->m_type == DT_DAYNUM ) continue;
		// skip if used in loop above! (in s1 contains s2)
		//if ( dx->m_flags & DF_USED3 ) continue;
		// accumulate the date types
		acc1 |= dxtype;//dx->m_hasType;
	}


	slot = -1;
	// now loop over all dates in each section and see what types we got
	if ( last2 ) slot = ht->getSlot ( &last2 );
	// reset
	dateflags_t acc2 = 0;
	// loop over all dates that telescoped up to this sec.
	for ( ; slot >= 0 ; slot = ht->getNextSlot(slot,&last2) ) {
		// get it
		int32_t pn = *(int32_t *)ht->getValueFromSlot(slot);
		// get the date index
		Date *dx = m_datePtrs[pn];
		// get its type
		datetype_t dxtype = dx->m_hasType ;
		// convert a holiday like thanksgiving to a month and daynum
		if ( dxtype & DT_HOLIDAY ) {
			// take it out
			dxtype &= ~DT_HOLIDAY;
			// put something else in its place maybe
			if ( dx->m_suppFlags & SF_NORMAL_HOLIDAY ) {
				//dxtype |= DT_MONTH|DT_DAYNUM;
				dxtype |= DT_DAYNUM;
			}
			// everything except generic "holiday" word
			else if ( dx->m_num != HD_HOLIDAYS ) {
				dxtype |= DT_DAYNUM;
			}
		}
		// skip if isolated daynum - they are noisy and often wrong
		//if ( dx->m_type == DT_DAYNUM ) continue;
		// accumulate the date types
		acc2 |= dxtype;//dx->m_hasType;
		// mark this date as used
		//dx->m_flags |= DF_USED3;
	}

	slot = -1;
	// now loop over all dates in each section and see what types we got
	if ( last3 ) slot = ht->getSlot ( &last3 );
	// reset
	dateflags_t acc3 = 0;
	// loop over all dates that telescoped up to this sec.
	for ( ; slot >= 0 ; slot = ht->getNextSlot(slot,&last3) ) {
		// get it
		int32_t pn = *(int32_t *)ht->getValueFromSlot(slot);
		// get the date index
		Date *dx = m_datePtrs[pn];
		// get its type
		datetype_t dxtype = dx->m_hasType ;
		// convert a holiday like thanksgiving to a month and daynum
		if ( dxtype & DT_HOLIDAY ) {
			// take it out
			dxtype &= ~DT_HOLIDAY;
			// put something else in its place maybe
			if ( dx->m_suppFlags & SF_NORMAL_HOLIDAY ) {
				//dxtype |= DT_MONTH|DT_DAYNUM;
				dxtype |= DT_DAYNUM;
			}
			// everything except generic "holiday" word
			else if ( dx->m_num != HD_HOLIDAYS ) {
				dxtype |= DT_DAYNUM;
			}
		}
		// accumulate the date types
		acc3 |= dxtype;//dx->m_hasType;
	}
	*/

	// . convert DT_DOW to DT_DAYNUM
	// . this was stopping "Fridays & Saturdays at 8pm" from telescoping
	//   to "November 27 - December 19, 2009"
	//if ( acc1 & DT_DOW ) { acc1 &= ~DT_DOW; acc1 |= DT_DAYNUM; }
	//if ( acc2 & DT_DOW ) { acc2 &= ~DT_DOW; acc2 |= DT_DAYNUM; }

	// . treat holidays as daynums too
	// . fix so "valentine's day" does not telescope to tod range
	//   in above section that has a month/daynum for
	//   http://outside.in/places/albuquerque-mennonite-church-albuquerque
	//if ( acc1 & DT_HOLIDAY ) { acc1 &= ~DT_HOLIDAY; acc1 |= DT_DAYNUM; }
	//if ( acc2 & DT_HOLIDAY ) { acc2 &= ~DT_HOLIDAY; acc2 |= DT_DAYNUM; }
	//if ( acc3 & DT_HOLIDAY ) { acc3 &= ~DT_HOLIDAY; acc3 |= DT_DAYNUM; }

	// fix for santafeplayhouse.org from allowing one tod to telescope
	// into another soft section that also has a month/daynum and use its
	// dow... see above where inSame in set for more comments
	if ( inSame ) {
		// just look at month and daynums
		if ( ! (acc1 & DT_DAYNUM ) ) return 1;
		if ( ! (acc1 & DT_MONTH  ) ) return 1;
		if ( ! (acc2 & DT_DAYNUM ) ) return 1;
		if ( ! (acc2 & DT_MONTH  ) ) return 1;
	}

	// . if both have daynums, then do not allow
	// . fixes peachpundit.com from telescoping its pubdate to the
	//   tod range in the article, which belongs to a month/daynum 
	//   mentioned in the article.
	// . crap this also breaks signmeup.com
	// . this breaks thewoodencow.com because the store hours section
	//   contains a list of monthdays below it which is included in acc1
	// . it no longer seems to be needed by peachpundit.com but taking
	//   it out caused a few anomalies. not sure if good or bad really,
	//   so i left this in and fixed thewoodencow.com
	if ( (acc1 & DT_DAYNUM) && (acc2 & DT_DAYNUM) &&
	     // fix thewoodencow.com
	     ! ( dp->m_flags & DF_STORE_HOURS) &&
	     // but this breaks santefeplayhouse.org from telescoping
	     // a daynum to a range "Dec x - Jan y", so allow range headers
	     // to be compatible
	     !(acc1 & (DT_RANGE|DT_RANGE_MONTHDAY|DT_RANGE_DAYNUM)) &&
	     // along the same lines allow lists
	     // breaks www.missioncvb.org which has
	     // "10:15 pm on both Friday and Saturday night" telescoping to
	     // "Friday, May 9 and Saturday, May 10, 2008"
	     !(acc1 & (DT_LIST_ANY)) )
		return 0;

	// if the header section we are trying to telescope to, completely
	// contains the date types in our last date in the telscope, then
	// do not allow it. fixes "April 2011 [[]] SUN [[]] 10 [[]] 3pm"
	// where the "3pm" was actually in the same <td> section as
	// another daynum, "3", so the "10" daynum should not have been
	// allowed to telescope to it! fix for url
	// http://www.zvents.com/z/las-cruces-nm/classics-performance-by-
	// terrence-wilson-piano--events--129171945
	// like above rule but we use "DD" not acc2...
	if ( (acc1 & DT_DAYNUM) && (DD->m_hasType & DT_DAYNUM) &&  
	     // fix thewoodencow.com
	     ! ( dp->m_flags & DF_STORE_HOURS) &&
	     // if its a range of daynums, probably ok
	     !(acc1 & (DT_RANGE|DT_RANGE_MONTHDAY|DT_RANGE_DAYNUM))  )
		return 0;

	// for blackbird buvette "first Thursday" telescopes up to 
	// the section right above and grabs the "December 3, 12:30PM" when
	// it shouldn't all because our date is "December 3, 12:00-12:00AM"
	// which is a range. so make a mask now to exclude ranges, lists,
	// etc. and just focus on the basic types.
	dateflags_t mask = DT_DOW|DT_TOD|DT_MONTH|DT_YEAR|DT_DAYNUM;
	// now include a monthday range so the header can be like
	// "April 19 - November 30, 2008" and all dates in its subsection can
	// telescope up to it. fixes graffiti.org
	mask |= DT_RANGE_MONTHDAY;

	// allow "Monday, November 23, 2009" to go to "Mon | Tue | Wed" for
	// mrmovietimes.com (and vice versa)
	// no, because it hurts st-margarets.com, so put an exception for
	// mrmovietimes.com up above specifically for this case
	//mask |= DT_LIST_DOW;
	// mask out
	acc1 &= mask;
	acc2 &= mask;
	acc3 &= mask;

	// if the section under the header section has an acc2 of zero that
	// basically means all the dates stemmed each other off in a subsection
	// and none were able to "blow out" of that subsection. this fixes
	// santfeplayhouse tuna xmas from not getting the range:
	// "dec 10 2009 - jan 3 2010"
	if ( acc2 == 0 ) return 1;
	// not sure if we should do this one though...
	// yeah because acc1 only has DT_HOLIDAY set and the mask makes it zero
	// so we lose "Thanksgiving", and acc2 has a single TOD, and this
	// prevents them from being compatible. fixes signmeup.com.
	if ( acc1 == 0 ) return 1;

	// see if one is subset of the other
	if ( (acc1 & acc2) == acc1 ) return 0;


	// . fix folkmads.org so tods can telescope to the month/day/tod date
	// . allows "5-6 pm[[]] Saturday, July 24, 2-5 pm"
	// . allows "7-10:30 pm[[]] Saturday, July 24, 2-5 pm"
	// . allow a tod/todrange to telescope to another date that has a tod
	//   provided of course they are not datebrothers
	// . this changed caused a title change for abqcsl.org but that's it
	// . crap this was causing the comment tod for piratecatradio.com
	//   to telescope to the play time and address, so until we somehow
	//   are sure the tod is not a comment tod we have to leave this out
	//if ( ! s3 && ( acc2 == (DT_RANGE_TOD|DT_TOD) || acc2 == DT_TOD ) )
	//	return 1;

	// likewise, a fix for villr.com so a monthday can telescope to
	// a store hours thingy
	// screws up http://www.glsc.org/visit/omnimax.php?id=45
	// TODO: fix better by allowing to also telescope to the 
	// monthday range in the parent section, then it will be ok.
	//if ( ! s3 && acc2 == (DT_MONTH|DT_DAYNUM) ) 
	//	return 1;

	// fix "8:30 to midnight [[]] Wednesdays at 7:00 pm" for
	// salsapower.com.
	if ( di->m_hasType == (DT_TOD|DT_RANGE_TOD) &&
	     (dp->m_hasType & DT_TOD) &&
	     dp->m_minTod <= di->m_minTod )
		return 1;

	// this hurts culturemob.com by stopping:
	// "9:00pm Wed  [[]] Wednesday, November 25, 2009 9:00 PM"
	// but without this constraint like 15 pages are bad! this rule
	// functions like our email/phone number algo, by preventing one
	// event from sharing datees with another...
	if ( (acc1 & acc2) == acc2 ) return 0;

	// . now to fix "December 2009 [[]] 12/01 [[]] 4pm" for 770kob.com
	// . if the section that contains 4pm fully contains all types in
	//   s3, then we should not telescope to it
	if ( s3 && (acc1 & (acc2|acc3)) == (acc2|acc3) ) return 0;

	// stop Sep 1 - Sep 25 [[]] Wed - Sat [[]] noon - 5pm
	// for thewoodencow.com.
	// s3 is "Wed - Sat" section and acc1 is "noon - 5pm" section
	// the "noon - 5pm" belongs with "Sunday" not "Wed - Sat". these two
	// critters are in different sections i think, so this fixes that.
	//if ( s3 && (acc1 & acc3) == acc1 ) return 0;
	// no, header can completely contain the last date's section because
	// for adobetheater we have "sunday 2pm [[]] July " and the needs to
	// telescope to "July 9th - August 1st, 2010"
	//if ( s3 && (acc1 & acc3) == acc3 ) return 0;


	// fix burtstikilounge.com so once we telescope to the store hours
	// tod range of 8pm - 2am then we do not go on to telescope to
	// a dow in the calendar, but only to the "Monday - Saturday" in
	// the s3 section. so if s3 fully contains all our header section
	// types, do not allow it to be telescoped to.
	// so we have
	// "29 [[]] November 2009 [[]] 8pm - 2am [[]] Monday - Saturday"
	// which is good, but we also have the bad:
	// "29 [[]] November 2009 [[]] 8pm - 2am [[]] Sun" which is bad
	// because we are closed sundays! and once you telescope to those
	// store hours range of "8pm - 2am" you shouldn't be allowed to
	// telescope to those dows back in the calendar since 
	// "Monday - Saturday" is your topologically nearest dow/dowrange
	// from "8pm - 2am" in the store hours sections.
	if ( s3 && (acc1 & acc3) == acc1 ) return 0;

	// otherwise, they are not the same elements in a list per se, so
	// we can pair them together
	return 1;
}


// return 0 for false, 1 for true and -1 on error
int32_t Dates::isCompatible2 ( Section *s1 , Section *s2 , bool useXors ) {

	if ( s1 == s2 ) return 1;

	// get our phone table, will set it if needs to
	//HashTableX *pt = NULL;
	// get our email table, will set it if needs to
	//HashTableX *et = NULL;
	// null if not used
	//HashTableX *at = NULL;
	//HashTableX *rt = NULL;

	//if ( usePhoneTable ) pt = getPhoneTable();
	// get our email table, will set it if needs to
	//if ( useEmailTable ) et = getEmailTable();
	// only get if requested
	//if ( usePlaceTable ) at = m_addresses->getPlaceTable();
	// different events have different prices
	//if ( usePriceTable ) rt = getPriceTable();

	// if last and last2 both have the same item like a phone number,
	// email, subfield name in tags, field name with a colon after it,
	// same "at the" phrase, or "at: " phrase, "location" word, cost/price,
	// tod, etc. then they are not compatible. the header should
	// not have such things in common with the headee, otherwise it is
	// more likely just another item in a list!
	int32_t phFinal1 = 0;
	int32_t ehFinal1 = 0;
	int32_t ahFinal1 = 0;
	// . -1 indicates none, since free is a cost of "0".
	// . no, now free is like 999999
	int32_t priceFinal1 = 0;

	// blow up "s1" until we hit last section that does NOT contain "s2"
	Section *last1 = s1;
	for ( Section *si = s1 ; si ; si = si->m_parent ) {
		// breathe
		QUICKPOLL(m_niceness);
		// if si contains s2 then stop
		if ( si->contains ( s2 ) ) break;
		/*
		if ( pt ) { 
			int64_t *ph = (int64_t *)pt->getValue ( &si );
			if ( ph && ( phFinal1 ^ *ph ) ) phFinal1 ^= *ph;
		}
		// get any email hash as we go along
		if ( et ) {
			int64_t *eh = (int64_t *)et->getValue ( &si );
			if ( eh && ( ehFinal1 ^ *eh ) ) ehFinal1 ^= *eh;
		}
		if ( rt ) {
			int32_t *price = (int32_t *)rt->getValue ( &si );
			if ( price && priceFinal1 == -1 )
				priceFinal1 = *price;
		}
		// address table, data values are Address ptrs really
		//if ( at ) {
		//	// these are Address indexes really
		//	int32_t *ah = (int32_t *)at->getValue ( &si );
		//	if ( ah && ( ahFinal1 ^ *ah ) ) ahFinal1 ^= *ah;
		//}
		// . address table, data values are Address ptrs really
		// . we now try to get the address because for graffiti.org
		//   the Denver Botanical Gardnes was mentioned in the date
		//   header (last1) and had its address in last2, which was
		//   what the alias was referring to, but we didn't realize
		//   they were really the same place! so fix that with this
		//   new logic here.
		if ( at ) {
			// key mixing now
			int32_t key = hash32h((int32_t)si,456789);
			// these are Address indexes really
			Place **pp = (Place **)at->getValue ( &key );
			// get the address?
			if ( pp ) {
				// get that
				Address *ad = (*pp)->m_address;
				// assume none
				int32_t h = 0;
				// or alias
				if ( ! ad ) ad = (*pp)->m_alias;
				// or just use place hash i guess!
				if ( ! ad ) h = (int32_t)*pp;
				// otherwise hash up address street etc.
				else {
					h =(int32_t)ad->m_street->m_hash;
					h^=(int32_t)ad->m_street->m_streetNumHash;
					//h ^= ad->m_adm1->m_cid; // country id
					//h ^= (int32_t)ad->m_adm1Bits;
					//h ^= (int32_t)ad->m_cityHash;
					h ^= (int32_t)ad->m_cityId32;
					// sanity check
					//if ( ! ad->m_adm1Bits ||
					//     ! ad->m_cityHash ) {
					if ( ! ad->m_cityId32 ) {
					     //! ad->m_adm1->m_cid  ) {
						char *xx=NULL;*xx=0; }
				}
				// old way
				//h = (int32_t)*pp;
				// and use that now
				if ( ( ahFinal1 ^ h ) ) ahFinal1 ^= h;
			}
		}
		*/
		// assign this if doesn't contain it yet
		last1 = si;
	}

	// . if no such section, i guess we share the same section
	// . i guess we are compatible then...
	//if ( ! last1 ) return 1;

	// get any phone number hash as we go along
	if ( last1 && useXors ) {
		phFinal1    = last1->m_phoneXor;
		ehFinal1    = last1->m_emailXor;
		priceFinal1 = last1->m_priceXor;
		ahFinal1    = last1->m_addrXor;
	}


	int64_t phFinal2 = 0;
	int64_t ehFinal2 = 0;
	int64_t ahFinal2 = 0;
	int32_t      priceFinal2 = 0;

	// blow up "s2" until we hit last section that does NOT contain "s2"
	Section *last2 = s2;
	for ( Section *si = s2 ; si ; si = si->m_parent ) {
		// breathe
		QUICKPOLL(m_niceness);
		// if si contains s1 then stop
		if ( si->contains ( s1 ) ) break;
		/*
		// get any phone number hash as we go along
		if ( pt ) { 
			int64_t *ph = (int64_t *)pt->getValue ( &si );
			if ( ph && ( phFinal2 ^ *ph ) ) phFinal2 ^= *ph;
		}
		// get any email hash as we go along
		if ( et ) {
			int64_t *eh = (int64_t *)et->getValue ( &si );
			if ( eh && ( ehFinal2 ^ *eh ) ) ehFinal2 ^= *eh;
		}
		if ( rt ) {
			int32_t *price = (int32_t *)rt->getValue ( &si );
			if ( price && priceFinal2 == -1 )
				priceFinal2 = *price;
		}
		// address table, data values are Address ptrs really
		//if ( at ) {
		//	// these are Address indexes really
		//	int32_t *ah = (int32_t *)at->getValue ( &si );
		//	if ( ah && ( ahFinal2 ^ *ah ) ) ahFinal2 ^= *ah;
		//}
		// address table, data values are Address ptrs really
		if ( at ) {
			// key mixing now
			int32_t key = hash32h((int32_t)si,456789);
			// these are Address indexes really
			Place **pp = (Place **)at->getValue ( &key );
			// get the address?
			if ( pp ) {
				// get that
				Address *ad = (*pp)->m_address;
				// assume none
				int32_t h = 0;
				// or alias
				if ( ! ad ) ad = (*pp)->m_alias;
				// or just use place hash i guess!
				if ( ! ad ) h = (int32_t)*pp;
				// otherwise hash up address street etc.
				else {
					h =(int32_t)ad->m_street->m_hash;
					h^=(int32_t)ad->m_street->m_streetNumHash;
					//h ^= ad->m_adm1->m_cid; // country id
					//h ^= (int32_t)ad->m_adm1Bits;
					//h ^= (int32_t)ad->m_cityHash;
					h ^= (int32_t)ad->m_cityId32;
					// sanity check
					//if ( ! ad->m_adm1Bits ||
					//     ! ad->m_cityHash ) {
					if ( ! ad->m_cityId32 ) {
					     //! ad->m_adm1->m_cid  ) {
						char *xx=NULL;*xx=0; }
				}
				// old way
				//h = (int32_t)*pp;
				// and use that now
				if ( ( ahFinal2 ^ h ) ) ahFinal2 ^= h;
			}
		}
		*/
		// assign this if doesn't contain it yet
		last2 = si;
	}

	// get any phone number hash as we go along
	if ( last2 && useXors ) {
		phFinal2    = last2->m_phoneXor;
		ehFinal2    = last2->m_emailXor;
		priceFinal2 = last2->m_priceXor;
		ahFinal2    = last2->m_addrXor;
	}

	// likewise, sanity check
	//if ( ! last2 ) return 1;

	// . if not brothers do not bother with this algo really
	// . fixes santafeplayhouse's December 10, 2009 - January 3, 2010
	//   date range which has a location "at Widgetbox" in it which
	//   was preventing it from being a header!
	//if ( last1->m_tagHash != last2->m_tagHash ) return 1;


	if ( phFinal1 && phFinal2 && phFinal1 != phFinal2 ) 
		return 0;
	if ( ehFinal1 && ehFinal2 && ehFinal1 != ehFinal2 ) 
		return 0;
	if ( ahFinal1 && ahFinal2 && ahFinal1 != ahFinal2 ) 
		return 0;
	if ( priceFinal1 && priceFinal2 && priceFinal1 != priceFinal2 )
		return 0;

	//////////////////////////////////
	//
	// subfield detection
	//
	// . compare fields between last1 and last2, if they have some
	//   tags that have exactly the same text phrase in them
	//   those are probably fields.
	//
	//////////////////////////////////

	if ( last1 == last2 ) return 1;

	if ( last1->contains(last2) ) return 1;
	if ( last2->contains(last1) ) return 1;

	//return 1;

	// . hash each subsection's tagHash and content into here
	//   for subfield detection
	// . if s1 and s2 share one or more such hash then they are not
	//   compatible
	HashTableX *sft = getSubfieldTable();

	// now make the subfield table map a section ptr to a bit array
	// (32 bits initially) where each bit stands for some field that
	// is repeated. then if last1 and last2 have a bit in common that
	// means they have a field in common and are not compatible
	// use new method for testing against old
	int32_t *bits1 = (int32_t *)m_bitTable.getValue(&last1);
	int32_t *bits2 = (int32_t *)m_bitTable.getValue(&last2);
	bool compat = true;
	int32_t ni = m_numLongs; // InBitTable;
	if ( ! bits1 || ! bits2 ) ni = 0;
	for ( int32_t i = 0 ; i < ni ; i++ ) {
		if ( bits1[i] & bits2[i] ) { compat = false; break; }
	}

	// new code only for now
	return compat;

	// accumulate subfield hashes into this table, we will get a list
	// of them and we have to compare lists
	HashTableX cmp1;
	char cbuf1[130000];
	// just init it, fast and does not allocate
        cmp1.set(4,0,256,cbuf1,130000,false,m_niceness,"dates-cmp1");

	// for log
	int64_t start = gettimeofdayInMilliseconds();

	// now for section "last1" get range of all subsections to scan
	//for(int32_t i = last1->m_sortedIndex ; i<m_sections->m_numSections;i++){
	for ( Section *si1 = last1 ; si1 ; si1 = si1->m_next ) {
		// this section may have hashed multiple keys if it had
		// multiple fields in it
		//Section *si1 = m_sections->m_sorted[i];
		// stop if this section not contain in last1
		if ( si1->m_a >= last1->m_b ) break;
		// scan last1 and all all text sections into cmp1
		int32_t slot1 = sft->getSlot ( &si1 );
		for ( ; slot1 >= 0 ; slot1 = sft->getNextSlot(slot1,&si1) ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get its tagHash^contentHash value
			int32_t h = *(int32_t *)sft->getValueFromSlot(slot1);
			// add to table, just the key
			if ( ! cmp1.addKey ( &h ) ) {
				if ( ! g_errno ) { char *xx=NULL;*xx=0; }
				return -1;
			}
		}
	}

	int64_t took = start - gettimeofdayInMilliseconds();

	// for log
	if ( took > 2 ) log("dates: CHECK subfield took %"INT64" ms",took);

	// do the same subsection scan for last2
	//for( int32_t i = last2->m_sortedIndex;i<m_sections->m_numSections;i++) {
	for ( Section *si2 = last2 ; si2 ; si2 = si2->m_next ) {
		// this section may have hashed multiple keys if it had
		// multiple fields in it
		//Section *si2 = m_sections->m_sorted[i];
		// stop if this section not contain in last1
		if ( si2->m_a >= last2->m_b ) break;
		// now scan the hashes in last2 and see which are in "cmp1"
		int32_t slot2 = sft->getSlot ( &si2 );
		for ( ; slot2 >= 0 ; slot2 = sft->getNextSlot(slot2,&si2) ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get its tagHash^contentHash value
			int32_t h = *(int32_t *)sft->getValueFromSlot(slot2);
			// if this same guy is in last1, that is bad
			if ( cmp1.isInTable(&h) ) {
				// sanity check
				if ( compat ) { char *xx=NULL;*xx=0;}
				return 0;
			}
		}
	}

	// sanity check
	if ( ! compat ) { char *xx=NULL;*xx=0; }
	return 1;
}

#define MAXBYTES 1024

HashTableX *Dates::getSubfieldTable ( ) {
	// return it if we got it
	if ( m_sftValid ) return &m_sft;
	// scan the sections
	int32_t ns = m_sections->m_numSections ;
	// for log
	//log("dates: subfield start");
	// just init it, fast and does not allocate
	//m_sft.set(4,4,128,NULL,0,true,m_niceness);
	// count what we need
	int32_t needSlots = 0;
	// loop it
	for ( int32_t k = 0 ; k < ns ; k++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get section
		Section *sk = &m_sections->m_sections[k];
		// skip if has no text itself
		if ( sk->m_flags & SEC_NOTEXT ) continue;
		// get its depth
		needSlots += sk->m_depth;
	}
	// double it for speed
	needSlots *= 4;

	// alloc it. return NULL with g_errno set on error
	if ( ! m_sft.set(4,4,needSlots,NULL,0,true,m_niceness,"m_sft") )
		return NULL;
	
	// dup field table
	HashTableX dt;
	if (!dt.set(4,4,5000,NULL,0,false,m_niceness,"dupfields")) return NULL;

	// maps 32bit field name hash to sections that have it directly
	HashTableX hts;
	if(!hts.set(4,4,5000,NULL,0,true,m_niceness,"sec-fields")) return NULL;

	// loop it
	for ( int32_t k = 0 ; k < ns ; k++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get section
		Section *sk = &m_sections->m_sections[k];
		// skip if has no text itself
		if ( sk->m_flags & SEC_NOTEXT ) continue;
		// how is this?
		if ( sk->m_contentHash64 == 0 ) { char *xx=NULL;*xx=0; }
		// get the tag id the delimits section if any
		//int32_t a = sk->m_a;
		// might not be there
		//nodeid_t tid = m_tids[a];
		// if it is bold, ignore it
		//if ( tid == TAG_B ) continue;
		// hash tag id and its content hash together
		int32_t h = sk->m_contentHash64 ^ sk->m_tagHash;
		// 0 is bad
		if ( h == 0 ) { char *xx=NULL;*xx=0; }
		// debug point
		//if ( h == -508009735 ) { char *xx=NULL;*xx=0; }
		// just one section now
		if ( ! m_sft.addKey ( &sk , &h ) ) return NULL;

		// sanity
		if ( ! sk ) { char *xx=NULL;*xx=0; }
		// find duplicated subfields
		if ( ! dt.addTerm32 ( &h ) ) return NULL;
		// map hash to section as well now for new loop below
		if ( ! hts.addKey ( &h , &sk ) ) return NULL;

		// test this
		//continue;
		// gotta add to all parents!
		//for ( ; sk ; sk = sk->m_parent ) {
		//	// breathe
		//	QUICKPOLL(m_niceness);
		//	// . key is the section ptr!
		//	// . return NULL with g_errno set on error
		//	if ( ! m_sft.addKey ( &sk , &h ) ) return NULL;
		//}
	}

	// now scan the words for fields preceeding colons, like "At:"
	// or "Squares:"
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if not alnum
		if ( ! m_wids[i] ) continue;
		// colon must follow
		if ( i+1>= m_nw || m_wptrs[i+1][0] != ':' ) continue;
		// must not be a digit (stop 4:30pm)
		if ( is_digit(m_wptrs[i][0]) ) continue;
		// back up up to 4 alnum words to look for tag
		int32_t kmin = i - 8;
		if ( kmin < 0 ) kmin = 0;
		// make hash of alnum words
		int32_t h = 0;
		// loop
		int32_t k ; for ( k = i ; k >= kmin ; k-- ) {
			// hash wids together
			if ( m_wids[k] ) {
				// see if its zero
				int32_t newh = h ^ (uint32_t)m_wids[k];
				//  use that if not zero
				if ( newh ) h = newh;
			}
			// skip if not tid
			if ( ! m_tids[k] ) continue;
			// got it
			break;
		}
		// skip word if not good
		if ( k < kmin ) continue;
		// 0 is bad
		if ( h == 0 ) { char *xx=NULL;*xx=0; }
		// debug point
		//if ( h == -508009735 ) { char *xx=NULL;*xx=0; }
		// get section
		Section *sk = m_sections->m_sectionPtrs[i];
		// just one section now
		if ( ! m_sft.addKey ( &sk , &h ) ) return NULL;

		// sanity
		if ( ! sk ) { char *xx=NULL;*xx=0; }
		// find duplicated subfields
		if ( ! dt.addTerm32 ( &h ) ) return NULL;
		// map hash to section as well now for new loop below
		if ( ! hts.addKey ( &h , &sk ) ) return NULL;

		// test this
		//continue;
		// gotta add to all parents!
		//for ( ; sk ; sk = sk->m_parent ) {
		//	// breathe
		//	QUICKPOLL(m_niceness);
		//	// . key is the section ptr!
		//	// . return NULL with g_errno set on error
		//	if ( ! m_sft.addKey ( &sk , &h ) ) return NULL;
		//}
	}

	// no longer use bitnum, use a list of 32-bit hashes for the fields
	// we contain. really just using sth (section to hash) table would
	// be nice. or better yet just make a buffer and store a ptr into
	// the section class that points into this buffer into a list of
	// "bit #'s" that are on. so a list like "5,33,99" or something.
	//uint64_t bitNum = 1LL;

	int32_t numBits = 0;
	// scan for the duplicated subfields, those are the only important
	// ones. then map them to an array of bits, up to 32 bits.
	for ( int32_t i = 0 ; i < dt.m_numSlots ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip empty slots
		if ( dt.m_flags[i] == 0 ) continue;
		// skip if not duplicated
		if ( dt.getScoreFromSlot(i) <= 1 ) continue;
		// count them
		numBits++;
	}
	// reset
	m_numLongs = 0;
	// do not redo this logic
	m_sftValid = true;
	// if none, bail
	if ( numBits <= 0 ) return &m_sft;


	int32_t bitNum = 0;
	if ( numBits > MAXBYTES*8 ) numBits = MAXBYTES*8;
	int32_t numLongs = (numBits+31)/32;
	// make it int32_t aligned for speed in checking intersections of
	// two different bitBufs in m_bitTable
	char bitBuf[MAXBYTES];
	if ( numLongs*4 > MAXBYTES ) { char *xx=NULL;*xx=0; }
	memset(bitBuf,0,numLongs*4);
	// init this now
	if ( ! m_bitTable.set(4,numLongs*4,256,NULL,0,false,m_niceness,
			      "subfields") )
		return NULL;
	// save this for checking bittable above
	m_numLongs = numLongs;

	for ( int32_t i = 0 ; i < dt.m_numSlots ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip empty slots
		if ( dt.m_flags[i] == 0 ) continue;
		// skip if not duplicated
		if ( dt.getScoreFromSlot(i) <= 1 ) continue;
		// get the hash (wordId limited to 32 bits)
		uint32_t *h = (uint32_t *)dt.getKeyFromSlot(i);

		// now what sections have that hash?
		int32_t slot = hts.getSlot(h);
		// must be there! dup table, dt, says so!
		if ( slot < 0 ) { char *xx=NULL;*xx=0; }
		// scan all sections that had this field name and make sure
		// their bit table entry has the bit for this field name
		for ( ; slot >= 0 ; slot = hts.getNextSlot(slot,h) ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get that section ptr
			Section **skp = (Section **)hts.getValueFromSlot(slot);
			// must be valid
			if ( ! skp || ! *skp ) { char *xx=NULL;*xx=0;}
			// get his bit array from the section ptr
			char *bits = (char *)m_bitTable.getValue(skp);
			// if not there add it
			if ( ! bits ) {
				// store it,return NULL if failed
				if ( ! m_bitTable.addKey(skp,bitBuf) )
					return NULL;
				// get it
				bits = (char *)m_bitTable.getValue(skp);
				// must be there now since we added it
				if ( ! bits ) { char *xx=NULL;*xx=0; }
			}
			// make a bitvec for this
			int32_t byteOff = bitNum / 8;
			char bitOff  = bitNum % 8;
			// set that
			bits[byteOff] |= (1<<bitOff);
		}
		// advance to another bit #
		bitNum++;
		// stop if we breach
		if ( bitNum >= numBits ) break;
	}
		
	// . now propagate your section's bits to all your parents
	// . use int32_t ptrs for speed
	for ( Section *si = m_sections->m_rootSection ; si ; si = si->m_next) {
		// breathe
		QUICKPOLL(m_niceness);
		// get our bits
		char *bits = (char *)m_bitTable.getValue(&si);
		// skip if none
		if ( ! bits ) continue;
		// otherwise telescope up
		Section *sp = si->m_parent;
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get parent bits
			char *pbits = (char *)m_bitTable.getValue(&sp);
			// if not there add it
			if ( ! pbits ) {
				// store it,return NULL if failed
				if ( ! m_bitTable.addKey(&sp,bitBuf) )
					return NULL;
				// get it
				pbits = (char *)m_bitTable.getValue(&sp);
				// must be there now since we added it
				if ( ! pbits ) { char *xx=NULL;*xx=0; }
				// re-get this since hashtable might have
				// and moved all the data around
				bits = (char *)m_bitTable.getValue(&si);
			}
			// or in each int32_t
			int32_t *dst = (int32_t *)pbits;
			int32_t *src = (int32_t *) bits;
			int32_t  count = 0;
			for ( ; count < numLongs ; count++ )
				// or in each int32_t
				*dst++ |= *src++;
		}
	}

	// for log
	//log("dates: subfield end");
	return &m_sft;
}		



// set Section::phoneXor member
void Dates::setPhoneXors ( ) {
	if ( m_phoneXorsValid ) return;
	m_phoneXorsValid = true;
	// set it
	for ( int32_t k = 0 ; k < m_nw ; k++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// use this so we can inc it
		int32_t i = k;
		// skip if tag or punct word
		if ( ! m_wids[i] ) continue;
		// need 3 digit number followed by hyphen then 4 digits
		if ( ! is_digit(m_wptrs[i][0]) ) continue;
		// must be 3 int32_t
		if ( m_wlens[i] != 3 ) continue;
		// skip that
		if ( ++i >= m_nw ) break;
		// this must have a hyphen
		if ( ! m_words->hasChar( i, '-' ) &&
		     // or could be 505.866.0715
		     ! m_words->hasChar( i, '.' )   )
			continue;

		// skip that
		if ( ++i >= m_nw ) break;
		// need 3 digit number followed by hyphen then 4 digits
		if ( ! is_digit(m_wptrs[i][0]) ) continue;
		// must be 3 int32_t
		if ( m_wlens[i] != 4 ) continue;
		// we got one!
		int64_t h64 = m_wids[i-2] ^ m_wids[i];
		// only need 32 bits
		int32_t h32 = (int32_t)h64;
		// get section
		Section *sp = m_sections->m_sectionPtrs[k];
		// telescope up!
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// propagate
			sp->m_phoneXor ^= h32;
			if ( ! sp->m_phoneXor ) sp->m_phoneXor = h32;
		}
	}
}		

// set Section::emailXor member
void Dates::setEmailXors ( ) {
	if ( m_emailXorsValid ) return;
	m_emailXorsValid = true;
	// set it
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if NOT punct word
		if ( m_wids[i] ) continue;
		if ( m_tids[i] ) continue;
		// int16_tcut
		char *p = m_wptrs[i];
		// skip if not @ sign
		if ( *p != '@' ) continue;
		// must be single char
		if ( m_wlens[i] != 1 ) continue;
		// scan that punct word
		char *pmin = p - 30;
		char *pmax = p + 30;
		if ( pmin < m_wptrs[0] ) pmin = m_wptrs[0];
		char *wend = m_wptrs[m_nw-1] + m_wlens[m_nw-1];
		if ( pmax > wend ) pmax = wend;
		// scan left
		char *left = p - 1 ;
		for ( ; left >= pmin ; left-- ) {
			// stop if we hit non name char
			if ( is_alnum_a (*left) ) continue;
			if ( *left == '.' ) continue;
			if ( *left == '-' ) continue;
			if ( *left == '_' ) continue;
			break;
		}
		// now the right for the subdomain
		char *right = p + 1;
		for ( ; right < pmax ; right++ ) {
			// stop if we hit non domain char
			if ( is_alnum_a (*right) ) continue;
			if ( *right == '.' ) continue;
			if ( *right == '-' ) continue;
			break;
		}
		// left starts with punct usually... unless hit pmin?
		if ( ! is_alnum_a(*left) ) left++;
		// stop if failed
		if ( right - p < 3 ) continue;
		if ( p - left  < 1 ) continue;
		// hash it up
		int64_t h32 = hash32Lower_utf8 ( left , right - left );
		// if a not found, keep scanning
		if ( h32 == 0LL ) continue;
		// get section
		Section *sp = m_sections->m_sectionPtrs[i];
		// telescope up!
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// propagate
			sp->m_emailXor ^= h32;
			if ( ! sp->m_emailXor ) sp->m_emailXor = h32;
		}
	}
}		

// . set price xors
// . use this two determine when two different sections are talking about
//   different events - the idea being they might list two different prices
// . include prices like "free" or references to prices, like "pay what u want"
void Dates::setPriceXors ( ) {
	if ( m_priceXorsValid ) return;
	m_priceXorsValid = true;
	int32_t price;
	// init?
	static bool s_init56 = false;
	static int64_t h_free;
	if ( ! s_init56 ) {
		s_init56 = true;
		h_free = hash64n("free");
	}
	// set it
	for ( int32_t k = 1 ; k < m_nw ; k++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// use this so we can inc it
		int32_t i = k;
		// skip if tag or punct word
		if ( ! m_wids[i] ) continue;
		// might be free
		if ( m_wids[i] == h_free ) {
			// but we need hard punt on either side
			if ( i+1<m_nw && m_words->isSpaces(i+1) ) continue;
			if ( m_words->isSpaces(i-1)             ) continue;
			price = 99999999;
			goto addToTable;
		}
		// need 3 digit number followed by hyphen then 4 digits
		if ( ! is_digit(m_wptrs[i][0]) ) continue;
		// must have a dollar sign before
		if ( m_wptrs[i][-1] != '$' ) continue;
		// get as number, ignore after floating point
		price = m_words->getAsLong(i);
		// jump here
	addToTable:
		// hash that price
		int32_t h32 = (int32_t)m_wids[i];
		// we got one!
		//int64_t h = m_wids[i-2] ^ m_wids[i];
		// get section
		Section *sp = m_sections->m_sectionPtrs[k];
		// telescope up!
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// propagate
			sp->m_priceXor ^= h32;
			if ( ! sp->m_priceXor ) sp->m_priceXor = h32;
		}
	}
}		

// set Section::m_todXor
void Dates::setTODXors ( ) {
	if ( m_todXorsValid ) return;
	m_todXorsValid = true;
	// set it
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// skip if not in body
		if ( di->m_a < 0 ) continue;
		// . skip if pub date
		// . fixes trumba.com so pub date does not count as a todxor
		//   and cause eventbrothers in an rss item which makes us
		//   lose a lot of the event description
		if ( di->m_flags & DF_PUB_DATE ) continue;
		// skip if registration date
		if ( di->m_flags & DF_REGISTRATION ) continue;
		if ( di->m_flags & DF_NONEVENT_DATE  ) continue;
		if ( di->m_flags5 & DF5_IGNORE ) continue;
		// require a tod
		if ( ! (di->m_hasType & DT_TOD) ) continue;
		// skip if like in format:
		// "Feb 22, 2011 9:00 AM - Feb 24, 2011 4:00 PM"
		// call that DF_RANGE_DAYNUMTOD format
		if ( di->m_hasType & DT_RANGE_TIMEPOINT ) continue;
		// do not venture into telescope section
		//if ( di->m_type == DT_TELESCOPE ) break;
		// if we are a telescope, get the first date in telescope
		if ( di->m_type == DT_TELESCOPE ) {
			// must telescope TO a tod in this case like
			// burtstikilounge.com
			//if ( di->m_ptrs[0] & 
			// "24 [[]] November 2009 [[]] 8pm - 2am [[]] 
			//  Monday - Saturday" for burtstikilounge.com
			di = di->m_ptrs[0];
		}
		// telescopes are not fuzzy
		else {
			// skip if fuzzy
			if ( di->m_flags & DF_FUZZY ) continue;
		}
		/*
		// get its hash
		int32_t a = di->m_a;
		int32_t b = di->m_b;
		// skip if not in body
		if ( a < 0 ) continue;
		char *sa = m_wptrs[a];
		char *sb = m_wptrs[b-1] + m_wlens[b];
		int64_t h = hash64 ( sa , sb - sa );
		*/
		// try this now
		uint64_t h = di->m_dateHash64;
		// make sure not zero
		if ( h == 0LL ) { char *xx=NULL;*xx=0; }
		// set section::todxor
		Section *sp = di->m_section; 
		// telescope up!
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// propagate
			sp->m_todXor ^= h;
			if ( sp->m_todXor == 0LL ) sp->m_todXor = h;
		}
	}
}		


// . set Section::m_dayXor
// . for dates that telescope to a TOD but do not have a tod in their
//   first date...
// . really JUST to fix calendar formats like burtstikilounge.com which have
//   a monthdaynum in the table cell which we need to call an eventbrother,
//   but it contains no tod per se
// . "24 [[]] November 2009 [[]] 8pm - 2am [[]] 
//    Monday - Saturday" for burtstikilounge.com
void Dates::setDayXors ( ) {
	if ( m_dayXorsValid ) return;
	m_dayXorsValid = true;
	// set it
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// if we are a telescope, get the first date in telescope
		if ( di->m_type != DT_TELESCOPE ) continue;
		// skip if pub date
		if ( di->m_flags & DF_PUB_DATE ) continue;
		//if ( di->m_flags & DF_COMMENT_DATE ) continue;
		// skip if registration date
		if ( di->m_flags & DF_REGISTRATION ) continue;
		if ( di->m_flags & DF_NONEVENT_DATE  ) continue;
		if ( di->m_flags5 & DF5_IGNORE ) continue;
		// require a tod somewhere in the telescope
		if ( ! (di->m_hasType & DT_TOD) ) continue;
		// get first date in telescope
		Date *fd = di->m_ptrs[0];
		// tod not allowed as first date, it should be covered
		// by the todxor logic
		if ( fd->m_hasType & DT_TOD ) continue;
		// . and base date must be daynum
		// . this means this is really only meant for calendars
		//   like you see on burtstikilounge.com...
		if ( fd->m_hasType != DT_DAYNUM ) continue;
		// get its hash
		int32_t a = fd->m_a;
		int32_t b = fd->m_b;
		// skip if not in body
		if ( a < 0 ) continue;
		char *sa = m_wptrs[a];
		char *sb = m_wptrs[b-1] + m_wlens[b];
		int64_t h = hash64 ( sa , sb - sa );
		// set section::m_dayXor
		Section *sp = fd->m_section; 
		// telescope up!
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// propagate
			sp->m_dayXor ^= h;
			if ( sp->m_dayXor == 0LL ) sp->m_dayXor = h;
		}
	}
}		

// . hash all possible fields owned by a section
// . just hash all the fragments
// . a fragment is a string of words delimeted by a tag or . : ( ) [ ]
//   or a date!
// . the period must be ending a sentence...

/*
HashTableX *Dates::getFieldTable ( ) {
	// . return it if we already computed it
	// . useful because Events.cpp needs us as well as Dates.cpp when
	//   calling Dates::isCompatible(), which Events.cpp also callsw
	if ( m_ftValid ) return &m_ft;
	// just init it, fast and does not allocate
	m_ft.set(4,4,0,NULL,0,false,m_niceness);
	// declare this
	HashTableX *tt = NULL;

	int64_t h = 0LL;

	// set it
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if NOT punct word
		if ( ! m_wids[i] ) continue;
		// scan the words for fields...

		// hash each field's hash into this section and all
		// parent sections until we hit a section containing a TOD


	}
	return &m_ft;
}
*/

//
// i would like to use this for the santefeplayhouse problem instead of
// using getFirstParentOfType() function.
//
// <div1>
// <div2>wednesday: dec 30 at 8pm</div2>
// Dec 11th
// </div1>
// Where "Dec 11th" telescopes to "8pm" when it shouldn't, but since
// it is in a section that contains <div2> then isCompatible() does not apply.
// we could probably fix it so that "dp" will "blow up" until it reaches
// the section containing it, "div1" in the example.

// telescope up date "dd" until it hits a section containing a date equal to dt
/*
Date *Dates::getFirstParentOfType ( Date *di, Date *last , HashTableX *ht) {

	// get our section
	Section *pp = m_sections->m_sectionPtrs[di->m_a];
	Date *best = NULL;
	// loop over "pp" and all its parents
	for ( ; pp ; pp = pp->m_parent ) {
		// pick the one *right above* us
		int32_t slot = ht->getSlot ( &pp );
		// loop over all dates that telescoped up to this sec.
		for ( ; slot >= 0 ; slot = ht->getNextSlot(slot,&pp) ) {
			// get it
			int32_t pn = *(int32_t *)ht->getValueFromSlot(slot);
			// get the date index
			Date *dp = m_datePtrs[pn];
			// skip if me
			if ( dp == di ) continue;
			// if us, return NULL
			if ( dp == last ) return NULL;
			// get the best
			if ( dp->m_hasType != last->m_hasType ) continue;
			// set best
			best = dp;
		}
		if ( best ) return best;
	}
	return best;
}
*/

bool Dates::addRanges ( Words *words , bool allowOpenEndedRanges ) {

	char       **wptrs = words->getWords    ();
	int32_t        *wlens = words->getWordLens ();
	int64_t   *wids  = words->getWordIds  ();
	nodeid_t    *tids  = words->getTagIds   ();

	// do not create ranges of ranges or lists
	dateflags_t skipFlags = DT_LIST_ANY | DT_RANGE_ANY;

	datetype_t specialTypes = 
		DT_HOLIDAY      | // thanksgiving
		DT_SUBDAY       | // mornings
		DT_SUBWEEK      | // weekends
		DT_SUBMONTH     | // last day of month
		DT_EVERY_DAY    | // 7 days a week
		DT_SEASON       | // summers
		DT_ALL_HOLIDAYS ; // "holidays"

	for ( int32_t i = 0 ; i < m_numDatePtrs - 1 ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if url date
		if ( di->m_a < 0 ) continue;

		//
		// first see if this is an open-ended range
		//
		// get previous two words
		int32_t pre = di->m_a - 1;
		// set a min
		int32_t min = di->m_a - 10;
		if ( min < 0 ) min = 0;
		// . previous word must be in same sentence otherwise
		//   "No cover before 9PM - $5 After"<br><br>"Every Sunday:..."
		//   puts the "after" with the "every sunday" for
		//   http://www.rateclubs.com/clubs/catch-one_14445.html
		// . we do not have sections at this point unfortunately so we
		//   can not check the sentence section...
		// . backup over tags and punct
		char brcount = 0;
		for ( ; pre > min ; pre-- ) {
			QUICKPOLL(m_niceness);
			if ( wids[pre] ) break;
			if ( ! tids[pre] ) continue;
			if ( ! isBreakingTagId(tids[pre]) ) continue;
			if ( ++brcount >= 2 ) break;
		}
		// get word before that
		int32_t pre2 = pre - 1;
		for ( ; pre2 > min ; pre2-- ) {
			QUICKPOLL(m_niceness);
			if ( wids[pre2] ) break;
			if ( ! tids[pre2] ) continue;
			if ( ! isBreakingTagId(tids[pre2]) ) continue;
			if ( ++brcount >= 2 ) break;
		}
		// zero it out if not there
		if ( pre2 <= min ) pre2 = -1;

		// do not check for open ended ranges if we shouldn't
		if ( ! allowOpenEndedRanges ) pre = min;
		
		// skip if nothing
		if ( pre > min && wids[pre] ) {
			dateflags_t of = 0;
			bool leftpt = false;
			if ( wids[pre] == h_thru    ) of = DF_ONGOING ;
			if ( wids[pre] == h_through ) of = DF_ONGOING ;
			if ( wids[pre] == h_though  ) of = DF_ONGOING ; //misp
			if ( wids[pre] == h_before  ) of = DF_ONGOING ;
			if ( wids[pre] == h_until   ) of = DF_ONGOING ;
			if ( wids[pre] == h_til     ) of = DF_ONGOING ;
			if ( wids[pre] == h_till    ) of = DF_ONGOING ;
			if ( wids[pre] == h_after   ) {
				leftpt = true; of = DF_ONGOING ; }
			// "begins|starts Nov 1" for unm.edu
			if ( di->m_hasType==(DT_MONTH|DT_DAYNUM|DT_COMPOUND)&&
			     ( wids[pre] == h_starts ||
			       wids[pre] == h_begins )  ){
				leftpt = false; of = DF_ONGOING ; }
			// "begins|starts on Nov 1"
			if ( di->m_hasType==(DT_MONTH|DT_DAYNUM|DT_COMPOUND)&&
			     wids[pre] == h_on &&
			     pre2 >= 0 &&
			     ( wids[pre2] == h_starts ||
			       wids[pre2] == h_begins )  ){
				leftpt = false; of = DF_ONGOING ; }
			if ( of ) {
				// through the summer means in the summer
				// TODO: use a subset of specialTypes here
				if ( ! ( di->m_type & specialTypes ) )
					di->m_flags |= of;
				// special flag for tods
				if ( di->m_type == DT_TOD ) {
					if ( leftpt )
						di->m_flags |= DF_AFTER_TOD;
					else
						di->m_flags |= DF_BEFORE_TOD;
					// remove exact flag
					di->m_flags &= ~DF_EXACT_TOD;
				}
				//di->m_flags |= DF_RANGE;
				di->m_a = pre;
				// might have 
				// "begins nov 1 and runs though dec 12" so we
				// can't give up on the connection now, we
				// might hight "through" next
				if ( leftpt ) continue;
			}
			// "ends at june 20th" "ends on june 20th"
			if ( (wids[pre] == h_at ||
			      wids[pre] == h_on ) &&
			     pre2 >= 0 &&
			     (wids[pre2] == h_ends ||
			      wids[pre2] == h_concludes ||
			      wids[pre2] == h_conclude) ) {
				di->m_flags |= DF_ONGOING;
				di->m_a = pre2;
				if ( di->m_type == DT_TOD )
					di->m_flags |= DF_BEFORE_TOD;
				// remove exact flag
				di->m_flags &= ~DF_EXACT_TOD;
				continue;
			}
		}
		// fix "schedule varies from weekday to weekend" because
		// it was causing a core in the addInterval logic because
		// the weekend is like endpoints for the weekday
		if ( di->m_type == DT_SUBWEEK ) continue;
		// get the neighbor to the right now
		int32_t j = i + 1;
		// int16_tcut
		Date *dj = NULL;
		// advance while ptr is NULL
		for ( ; j < m_numDatePtrs ; j++ ) {
			// assign
			dj = m_datePtrs[j];
			// skip modifiers like "through"
			//if ( dj && dj->m_type == DT_MOD ) continue;
			// skip if empty
			if ( dj ) break;
		}
		// forget it if still empty
		if ( ! dj ) break;
		// skip if url date, try another di then
		if ( dj->m_a < 0 ) continue;
		// . must be the same type
		// . exception: jan 3 to dec 4 2009 (ignore year)
		bool good = false;
		if ( (di->m_hasType & ~DT_YEAR) == (dj->m_hasType & ~DT_YEAR) )
			good = true;
		// also allow "Nov - Dec 5, 2008" (graffiti.org)
		if ( di->m_hasType == DT_MONTH &&
		     dj->m_hasType == (DT_MONTH|DT_DAYNUM|DT_YEAR|DT_COMPOUND))
			good = true;
		// also allow "Nov - Dec 5" (???)
		if ( di->m_hasType == DT_MONTH &&
		     dj->m_hasType == (DT_MONTH|DT_DAYNUM|DT_COMPOUND))
			good = true;
		// if not a good fit, skip this potential range right now
		if ( ! good ) continue;
		// skip if either is a list
		if ( di->m_hasType & skipFlags ) continue;
		if ( dj->m_hasType & skipFlags ) continue;
		// must be in ascending order! be it a dow,dom,month, year...
		//if ( di->m_num < dj->m_num ) continue;
		// get the word range between the two atomic dates
		int32_t a = di->m_b;
		int32_t b = dj->m_a;
		// too many words in between? forget it!
		if ( b - a > 20 ) continue;
		// count the associated alnums
		int32_t alnumcount = 0;
		// get word before us. looking for "between X and Y" phrase
		if ( pre > min && wids[pre] == h_between ) 
			alnumcount++;
		else
			pre = di->m_a;
		// init j for the scan of the junk between the two dates
		int32_t k = a;
		// count em
		int32_t andcount = 0;
		bool hyphen = false;
		int64_t prevWid = 0LL;
		bool brokenRange = false;
		int32_t badWords = 0;
		// scan what is between them to determine if is a range!
		for ( ; k < b ; k++ ) {
			// count em
			if ( wids[k] ) alnumcount++;
			// word? allow "to" for like "5 to 6pm"
			// word? allow "to" for like "aug 9 to aug 12"
			if ( wids[k] == h_to      ||
			     wids[k] == h_through ||
			     wids[k] == h_though  || // misspelling
			     wids[k] == h_before  ||
			     wids[k] == h_thru    ||
			     // "from nov 6 until nov 9"
			     wids[k] == h_until   ||
			     wids[k] == h_til     ||
			     wids[k] == h_till    ||
			     // "starting nov 6 and ongoing until dec 7"
			     wids[k] == h_ongoing ) {
				hyphen = true;
				continue;
			}

			// ends at: y
			if ( wids[k] == h_ends ) {
				hyphen      = true;
				prevWid     = h_ends;
				brokenRange = true;
				continue;
			}
			// end: y
			if ( wids[k] == h_end ) {
				hyphen      = true;
				prevWid     = h_end;
				brokenRange = true;
				continue;
			}
			// ends at: OR end at:
			if ( wids[k] == h_at && prevWid == h_ends ) continue;
			if ( wids[k] == h_at && prevWid == h_ends ) continue;
			
			// fix activedatax.com:
			// "start date: 10/7/2011 start time: 6:00pm
			//  end date: 10/7/2011 end time: 11:59pm"
			if ( wids[k] == h_date && prevWid == h_end ) continue;

			// facebooks's <start_time>....</start_time><end_time>
			if ( tids[k] == (TAG_FBSTARTTIME | BACKBIT) )
				continue;
			if ( tids[k] == TAG_FBENDTIME ) {
				hyphen = true;
				//brokenRange = true;
				continue;
			}

			// . this are not hyphens but they are transparent
			// . "starting nov 6 and continuing through dec 7"
			if ( wids[k] == h_continuing ) continue;
			if ( wids[k] == h_lasting    ) continue;
			if ( wids[k] == h_runs       ) 
				continue;
			if ( wids[k] == h_lasts      ) continue;
			if ( wids[k] == h_and ) {
				andcount++;
				continue;
			}
			// all other words break it
			//if ( wids[k] ) break;
			// no, might be an event end time field
			if ( wids[k] ) { badWords++; continue; }
			// <br> tag ok for microsoft front page
			//if ( tids[k] == TAG_BR ) continue;
			//if ( tids[k] == TAG_I  ) continue;
			//if ( tids[k] == TAG_B  ) continue;
			// all others, stop. unless non-breaking tag or a <br>
			// because i've seen
			// <strong>8:00</strong> - 10:30pm. for abqtango.org
			if ( tids[k] ) {
				// ok if not breaking
				if ( ! isBreakingTagId(tids[k]) ) continue;
				// br is ok for microsoft front page
				if ( tids[k] == TAG_BR ) continue;
				// <td> is ok for ci.tualatin.or.us which
				// has "starts at: x <td> ends at: y"
				if ((tids[k]&BACKBITCOMP) == TAG_TD ) continue;
				if ((tids[k]&BACKBITCOMP) == TAG_TR ) continue;
				// sometimes they use <time> xml-ish tags
				// like mcachicago.org
				if ((tids[k]&BACKBITCOMP) == TAG_XMLTAG ) 
					continue;
				// break on all else
				break;
			}
			// check this out
			//if ( wlens[k] > 3 ) break;
			// only allow space or hyphen for single char punct
			char *p    = wptrs[k];
			char *pend = p + wlens[k];
			for ( ; p < pend ; p++ ) {
				// space is ok
				if ( is_wspace_utf8(p) ) continue;
				// hyphen is ok
				if ( *p == '-' ) {
					hyphen = true;
					continue;
				}
				// : is ok to fix "ends at:"
				if ( *p == ':' && brokenRange ) continue;
				// period is ok ("sun. thru thur.")
				if ( *p == '.' ) continue;
				// allow comma to fix carnegieconcerts.com
				// type "12pm, - 5pm"
				if ( *p == ',' ) continue;
				/*
				// utf8 hyphen from unm.edu url
				// no longer needed since XmlDoc.cpp now 
				// converts all utf8 hyphens into ascii
				if ( p[0] == -30 &&
				     p[1] == -128 &&
				     p[2] == -109 ) {
					p += 2;
					hyphen = true;
					continue;
				}
				*/
				// . crazy utf8 space
				// . www.trumba.com/calendars/KRQE_Calendar.rss
				/*
				if (wptrs[k][0] == -62 &&
				    wptrs[k][1] == -96 &&
				    wptrs[k][2] == '-' &&
				    wptrs[k][3] == -62 &&
				    wptrs[k][4] == -96 )
					continue;
				*/
				// all others fail
				break;
			}
			// all others fail
			if ( p < pend ) break;
		}
		// just "and" by itself is not a range indicator
		if ( andcount && andcount == alnumcount ) continue;
		// skip if did not make it. we are not a range then.
		if ( k < b ) continue;
		// need a hyphen or equivalent to be a range
		if ( ! hyphen ) continue;
		// stop if had bad words
		if ( badWords && ! brokenRange ) continue;

		// if we are adding a range of daynums like "3-12" then
		// scan to the left of that to see if "age" or "children"
		// is before that and after the previous date...
		if ( di->m_type == DT_DAYNUM && di->m_a >= 0 ) {
			// get prev date
			Date *prev = NULL;
			for ( int32_t pi = i - 1 ; pi >= 0 ; pi-- ) {
				// breathe
				QUICKPOLL(m_niceness);
				prev = m_datePtrs[pi];
				if ( prev ) break;
			}
			// scan before us but do not go past "min"
			int32_t min = di->m_a - 20;
			// or the prev date
			if ( prev && prev->m_b > min ) min = prev->m_b;
			// sanity. i can equal "a"... i've seen that
			// for some asian language page.
			// http://www.zoneuse.com/
			if ( min > di->m_a ) { char *xx=NULL;*xx=0; }
			// are we an age range?
			bool age = false;
			// scan before us and remain in sentence and after
			// the previous date...
			for ( int32_t w = di->m_a - 1 ; w >= min ; w-- ) {
				// breathe
				QUICKPOLL(m_niceness);
				if ( wids[w] == h_children ) age = true;
				if ( wids[w] == h_age      ) age = true;
				if ( wids[w] == h_ages     ) age = true;
			}
			// do not add this as a daynum range if its age range
			if ( age ) continue;
			// set a max now
			int32_t max = di->m_b + 20;
			// do not breach our words array
			if ( max > m_nw ) max = m_nw;
			// scan right for "years" "youngster 2-12 years"
			for ( int32_t w = di->m_b ; w < max ; w++ ) {
				QUICKPOLL(m_niceness);
				// 2-12 years old
				if ( wids[w] == h_years ) age = true;
				// 2-12 year olds
				if ( wids[w] == h_year  ) age = true;
			}
			// do not add this as a daynum range if its age range
			// or year range
			if ( age ) continue;

		}

		// . fix "November - December 5, 2008" (graffiti.org)
		// . if a daynum follows 2nd month in range, skip it
		if ( di->m_type == DT_MONTH && dj->m_type == DT_MONTH ) {
			// get next date after dj
			Date *next = NULL;
			// scan to right of dj
			for ( int32_t nj = j+1 ; nj < m_numDatePtrs ; nj++ ) {
				QUICKPOLL(m_niceness);
				next = m_datePtrs[nj];
				if ( next ) break;
			}
			// must be right after us
			bool isdaynum = false;
			// is it a daynum after us?
			if ( next && next->m_type == DT_DAYNUM )
				isdaynum = true;
			// set max to scan
			int32_t kmax = dj->m_b + 10;
			// do not hit daynum
			if ( next && kmax > next->m_a ) kmax = next->m_a;
			// no scanning if not daynum
			if ( ! isdaynum ) kmax = -1;
			// and no words in between
			for ( int32_t k = dj->m_b ; k < kmax ; k++ ) {
				if ( ! wids[k] ) continue;
				// if we hit another alnum word in between
				// the dj month and daynum, then do not
				// count it as a daynum for the dj month
				isdaynum = false;
				break;
			}
			// skip if it is
			if ( isdaynum ) continue;
		}
		
		// use this now
		Date *DD;
		if ( di->m_type == DT_DOW ) {
			DD = addDate (DT_RANGE_DOW,0,pre,dj->m_b,0);
			if ( DD ) DD->m_dow = -1;
			// set all the m_dowBits now
			if ( DD ) {
				int32_t dow1 = di->m_num;
				int32_t dow2 = dj->m_num;
				if ( dow1 < 0 ) { char *xx=NULL;*xx=0; }
				if ( dow2 < 0 ) { char *xx=NULL;*xx=0; }
				// fix "Tuesday through Sunday"
				//if ( minDow > maxDow ) {
				//	int32_t tt = minDow;
				//	minDow = maxDow;
				//	maxDow = tt;
				//}
				for ( int32_t i = 1 ; i <= 7 ; i++ ) {
					// skip if not in range
					if ( dow1 <= dow2 ) {
						if ( i < dow1 ) continue;
						if ( i > dow2 ) continue;
					}
					// . strange range?
					// . i.e. "Tues through Sun"
					// . i.e. "Friday thru Monday"
					else if ( dow1 > dow2 ) {
						if ( i < dow1 && i > dow2 )
							continue;
					}
					// sanity check
					if ( i >= 8 ) { char *xx=NULL;*xx=0;}
					DD->m_dowBits |= (1<<(i-1));
				}
				//if(minDow > maxDow ) { char *xx=NULL;*xx=0;}
			}
		}
		else if ( di->m_type == DT_TOD ) {
			DD = addDate (DT_RANGE_TOD,0,pre,dj->m_b,0);
			if (DD ) DD->m_tod = -1;
		}
		else if ( di->m_type == DT_YEAR ) {
			DD = addDate (DT_RANGE_YEAR,0,pre,dj->m_b,0);
		}
		else if ( di->m_hasType == (DT_MONTH|DT_DAYNUM|DT_COMPOUND) )
			DD = addDate (DT_RANGE_MONTHDAY,0,pre,dj->m_b,0);
		// "Nov - Dec 5"
		else if ( di->m_type == DT_MONTH &&
			  dj->m_hasType == (DT_MONTH|DT_DAYNUM|DT_COMPOUND) )
			DD = addDate (DT_RANGE_MONTHDAY,0,pre,dj->m_b,0);
		// "Nov - Dec 5 2008"
		else if ( di->m_type == DT_MONTH &&
			  dj->m_hasType==(DT_MONTH|DT_DAYNUM|DT_YEAR|
					  DT_COMPOUND) )
			DD = addDate (DT_RANGE_MONTHDAY,0,pre,dj->m_b,0);
		// nov - dec
		else if ( di->m_hasType == DT_MONTH )
			DD = addDate (DT_RANGE_MONTH,0,pre,dj->m_b,0);
		else if ( di->m_type == DT_DAYNUM ) {
			DD = addDate (DT_RANGE_DAYNUM,0,pre,dj->m_b,0);
			if ( DD ) DD->m_dayNum = -1;
		}
		// trumba.com's 
		// "Friday, December 4, 1pm - Saturday, December 5, 2009, 4pm"
		else if ( di->m_hasType == (DT_TOD|DT_DAYNUM|DT_MONTH|
					    DT_DOW|DT_COMPOUND) ) {
			DD = addDate (DT_RANGE_TIMEPOINT,0,pre,dj->m_b,0);
			if ( DD ) DD->m_dayNum = -1;
		}
		// cfa.aiany.org has 
		// "Feb 22, 2011 9:00 AM - Feb 24, 2011 4:00 PM" and we
		// need that to NOT be an event brother so DT_RANGE_TIMEPOINT
		// needs to be set
		else if ( di->m_hasType == (DT_TOD|DT_DAYNUM|DT_MONTH|
					    DT_YEAR|DT_COMPOUND) ) {
			DD = addDate (DT_RANGE_TIMEPOINT,0,pre,dj->m_b,0);
			if ( DD ) DD->m_dayNum = -1;
		}
		// and another one just in case
		else if ( di->m_hasType == (DT_TOD|DT_DAYNUM|DT_MONTH|
					    DT_COMPOUND) ) {
			DD = addDate (DT_RANGE_TIMEPOINT,0,pre,dj->m_b,0);
			if ( DD ) DD->m_dayNum = -1;
		}
		else
			DD = addDate (DT_RANGE,0,pre,dj->m_b,0);
		// return false on error
		if ( ! DD ) return false;

		// 1pm-2am (need to add 24 hours to 2am)
		if ( di->m_type == DT_TOD && 
		     dj->m_num < di->m_num &&
		     dj->m_num < 12*3600 ) {
			// no, then Saturday 5pm-2am actually gets the
			// interval that is considered friday night
			//dj->m_num += 24*3600;
			// set this to that... for computing duration
			dj->m_truncated = dj->m_num ;
			// . so truncate to midnight
			// . no! might be 9pm-3am
			//dj->m_num    = 24*3600;
			//dj->m_tod    = 24*3600;
			//dj->m_minTod = 24*3600;
			//dj->m_maxTod = 24*3600;
			dj->m_num    += 24*3600;
			dj->m_tod    += 24*3600;
			dj->m_minTod += 24*3600;
			dj->m_maxTod += 24*3600;
			// note it. shift DEFINTION of day up by 2 hours
			// if "num" was like 2am...
			int32_t shiftDay = dj->m_num - 24*3600;
			if ( m_shiftDay && shiftDay > m_shiftDay )
				m_shiftDay = shiftDay;
			else if ( m_shiftDay == 0 )
				m_shiftDay = shiftDay;
			// dj is implied pm then
			dj->m_suppFlags |= SF_IMPLIED_AMPM;
		}

		// a quick fix for 12:00-12:00am, set di to noon then
		if ( di->m_num == 86400 && dj->m_num == 86400 ) {
			di->m_num    = 12*3600;
			di->m_tod    = 12*3600;
			di->m_minTod = 12*3600;
			di->m_maxTod = 12*3600;
			di->m_suppFlags |= SF_IMPLIED_AMPM;
		}

		// fix 12am-6pm (12am should be 0 not 86400)
		if ( di->m_num == 86400 && dj->m_num < 86400 ) {
			// make it midnight plus one second basically
			di->m_num    = 0;
			di->m_tod    = 0;
			di->m_maxTod = 0;
			di->m_minTod = 0;
		}

		// allow 10-5 or 9-5 to be implied
		//if ( di->m_num > dj->m_num ) {
		//	log("hey");
		//}

		// and set the ptrs
		DD->addPtr ( di , i , this );
		DD->addPtr ( dj , j , this );

		// sanity check
		//if ( di->m_num == dj->m_num ) { char *xx=NULL;*xx=0; }
		// force start back since first call to addPtr() sets it
		DD->m_a = pre;
	}
	return true;
}

// . now set the m_min* dates after "until", etc.
// . we could make a dummy date, and call addPtrs on it with all
//   the dates after the "until"
// . first make a dummy date based on spidered time and use that
//   as the range's first endpoint
// . also check for any date with a "through" or "ongoing" before
//   it and make that into a range as well
/*
void Dates::addOpenEndedRanges ( ) {

	// do not create ranges of ranges or lists
	dateflags_t skipFlags = DF_LIST | DF_RANGE;

	//
	// now look for "ongoing through Saturday, January 2, 2010" ...
	// and other open ended ranges.
	//
	for ( int32_t i = 0 ; i < m_numDatePtrs - 1 ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if is a list or range already
		if ( di->m_flags & skipFlags ) continue;
		// get previous two words
		int32_t pre = di->m_a - 2;
		// set a min
		int32_t min = di->m_a - 10;
		if ( min < 0 ) min = 0;
		// backup over tags and punct
		for ( ; pre > min && ! m_wids[pre] ; pre-- ) ;
		// skip if nothing
		if ( pre == min ) continue;
		// skip if not certain word
		if ( m_wids[pre] != h_thru    &&
		     m_wids[pre] != h_through &&
		     m_wids[pre] != h_until   ) continue;
		// update its start
		di->m_a = pre;
		// flag it as an open ended range
		di->m_flags |= DF_ONGOING;
		// now update the mins if min is valid
		if ( di->m_minDayNum != 32
	}

	// must have valid spider time
	if ( m_spideredTime <= 0 ) return;

	// or set ongoing flag in addPtrs() addDate() and when it is set
	// set the min 

	// parse that up
	struct tm *timeStruct ;
	timeStruct = localtime ( &m_spideredTime );

	// now loop over all dates with DF_ONGOING set either in the above
	// loop or in makeCompoundLists() and adjust the min endpoint.
	// similar to addPtr()
	for ( int32_t i = 0 ; i < m_numDatePtrs - 1 ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if not part of an open-ended range
		if ( ! ( di->m_flags & DF_ONGOING ) ) continue;
		// scan through each ptr looking for the ongoing flag
		for ( int32_t j = 0 ; j < di->m_numPtrs ; j++ ) {
			// int16_tcut
			Date *dj = di->m_ptrs[j];
			// skip if not part of an open-ended range
			if ( ! ( dj->m_flags & DF_ONGOING ) ) continue;
			// 

		// set the mins to the spidered time 

	// set m_minTime
	tm ts;
	memset(&ts, 0, sizeof(tm));
	ts.tm_mon  = m_minMonth - 1;
	ts.tm_mday = m_minDay;
	ts.tm_year = m_minYear - 1900;
	m_minTime = mktime(&ts);
	// integrate time of day
	m_minTime += m_minTOD;

mdw left off here
}
*/

bool Dates::addLists ( Words *words , bool ignoreBreakingTags ) {

	// for debug set this on MDWMDWMDW
	//ignoreBreakingTags = true;

	char       **wptrs = words->getWords    ();
	int32_t        *wlens = words->getWordLens ();
	int64_t   *wids  = words->getWordIds  ();
	nodeid_t    *tids  = words->getTagIds   ();

	// int16_tcut
	Section **sp = NULL;
	if ( m_sections ) sp = m_sections->m_sectionPtrs;

	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if nuked from range logic above
		if ( ! di ) continue;

		// skip if from url or not otherwise in the body
		if ( di->m_a < 0 ) continue;

		// don't make a list of DOW TODs like:
		// "Sun. - Thur. until 10pm Fri. + Sat. until Midnight"
		// http://blackbirdbuvette.com/
		// why not????
		//if ( di->m_hasType == (DT_TOD | DT_DOW) ) continue;

		// don't make a list of holidays because
		// "Thanksgiving, Christmas and New Year's Days"
		// needs to telescope the individual holidays to
		// "every day of the week, 9am-5pm" and
		// "Saturdays and Sundays in summer: 9am-6pm"
		// basically, each list item may need to telescope to
		// a different date header...
		if ( di->m_type == DT_HOLIDAY      ) continue;
		if ( di->m_type == DT_SUBDAY       ) continue;
		if ( di->m_type == DT_SUBWEEK      ) continue;
		if ( di->m_type == DT_SUBMONTH     ) continue;
		if ( di->m_type == DT_EVERY_DAY    ) continue;
		if ( di->m_type == DT_SEASON       ) continue;
		if ( di->m_type == DT_ALL_HOLIDAYS ) continue;

		// never make a list of calendar daynums
		if ( di->m_type == DT_DAYNUM && (di->m_flags & DF_IN_CALENDAR))
			continue;

		// reset this
		int32_t np = 0;

		Date *DD = NULL;
		// init the "j" loop
		int32_t j = i + 1 ;
		// int16_tcut
		Date *dj = NULL;
		// used for list of TODs
		bool lastHadAMPM = false;
		// record if we had am/pm
		if ( di->m_type==DT_TOD && (di->m_suppFlags & SF_HAD_AMPM) )
			// was it am or pm
			lastHadAMPM = true;

		// make date type for di
		datetype_t diHasType = di->m_hasType;
		// if we have a monthday range take out DT_EVERYDAY
		// so "Nov 11-12 daily 1pm" pairs with "Nov 13 2pm" in list
		if ( diHasType & DT_RANGE_MONTHDAY )
			diHasType &= ~DT_EVERY_DAY;
		if ( diHasType & DT_RANGE_DAYNUM )
			diHasType &= ~DT_EVERY_DAY;
		// list of tods is same as one tod or tod range. same
		// for daynums, months, etc.
		diHasType &= ~DT_LIST_ANY;
		diHasType &= ~DT_RANGE_ANY;

		// scan over atomic dates after the month
		for ( ; j < m_numDatePtrs ; j++ ) {
			// get the jth atomic date
			dj = m_datePtrs[j];
			// skip if nuked (was endpoint of a range i guess)
			if ( ! dj ) continue;

			// skip if from url or not otherwise in the body
			if ( dj->m_a < 0 ) continue;

			// never make a list with calendar daynums
			if ( dj->m_type == DT_DAYNUM && 
			     (dj->m_flags & DF_IN_CALENDAR))
				break;

			datetype_t djHasType = dj->m_hasType;
			// like above, if monthday range take out DT_EVERYDAY
			if ( djHasType & DT_RANGE_MONTHDAY )
				djHasType &= ~DT_EVERY_DAY;
			if ( djHasType & DT_RANGE_DAYNUM )
				djHasType &= ~DT_EVERY_DAY;
			// list of tods is same as one tod or tod range. same
			// for daynums, months, etc.
			djHasType &= ~DT_LIST_ANY;
			djHasType &= ~DT_RANGE_ANY;

			// does it match?
			bool match = (djHasType == diHasType );

			/*
			datetype_t dt1 = 
				DT_TOD|
				DT_DAYNUM|
				DT_MONTH|
				DT_COMPOUND|
				DT_LISTDAYNUM;
			*/
			// but do allow "Dec 13,20" to match "Jan 3"
			if ( ! match &&
			     (diHasType|DT_LIST_DAYNUM) ==
			     (djHasType|DT_LIST_DAYNUM) )
				match = true;

			// allow "Friday, May 9" + "and Saturday, May 10, 2008"
			// www.missioncvb.org
			if ( ! match &&
			     (diHasType|DT_YEAR) ==
			     (djHasType) )
				match = true;

			// allow for a list of 
			// "Sundays: Dec 13, 20, 27, Jan 3, at 2:00pm" and
			// "Mondays: Dec 21, 28, at 8:00pm"
			/*
			datetype_t dt2 = 
				DT_TOD|
				DT_DAYNUM|
				DT_MONTH|
				DT_COMPOUND|
				DT_LISTDAYNUM;
			*/
			if ( ! match &&
			     (diHasType | DT_LIST_OTHER) ==
			     (djHasType | DT_LIST_OTHER) )
				match = true;
			
			// allow for a list of:
			// "Tues., Wed., Thurs., 12 noon - 5 pm.;" and
			// "Fri. 9 am. - 2:30 pm."
			if ( ! match &&
			     (diHasType | DT_LIST_DOW) ==
			     (djHasType | DT_LIST_DOW) )
				match = true;

			// allow for a list of:
			// 8 am. Mon - Fri, 7:30 am - 10 am Sun.
			// for unm.edu
			if ( ! match &&
			     (diHasType | DT_RANGE_TOD|DT_RANGE_DOW) ==
			     (djHasType | DT_RANGE_TOD|DT_RANGE_DOW) )
				match = true;

			// allow blackbirdbuvetter.com to do
			// "Mon. - Fri. 11am - 2am" && "Sat. 12pm - 2am"
			// that way the kitchen hours which are
			// "Sun. - Thur. until 10pm" cam telescope to that
			// list and get all the hours correct!
			if ( ! match ) {
				datetype_t d1 = diHasType;
				datetype_t d2 = djHasType;
				if ( d1 & DT_RANGE_DOW ) 
					d1 &= ~DT_RANGE_DOW;
				if ( d2 & DT_RANGE_DOW ) 
					d2 &= ~DT_RANGE_DOW;
				if ( d1 == d2 ) 
					match = true;
			}

			// stop if not a day of month
			if ( ! match ) break;

			// TODO: possibly fix unm.edu which has: 
			// 9 am. - 6 pm. Mon. - Sat.<br>
			// Thur. 9 am. - 7 pm. Sun. 10 am - 4 pm.<br>"
			// and fails on gotBreak2 because dj->m_b hits the 
			// "Sun. 10 am..." date and not a breaking tag.
			Date *dx = NULL;
			for ( int32_t x = j + 1 ; 
			      x < m_numDatePtrs;x++) {
				QUICKPOLL(m_niceness);
				dx = m_datePtrs[x];
				if ( ! dx ) continue;
				break;
			}


			// declare outside for loop
			int32_t a;
			// scan words in between date "di" and date "dj"
			for ( a = di->m_b ; a < dj->m_a ; a++ ) {
				// "and" is ok
				if ( wids[a] == h_and ) continue;
				// Monday OR Friday at 1pm
				if ( wids[a] == h_or ) continue;
				// "and on" is ok or "on tuesday, on wed ..."
				// fixes http://www.law.berkeley.edu/140.htm 
				// "Monday to Saturday at 10 a.m. and on
				//  Sunday at 1 p.m"
				if ( wids[a] == h_on ) continue;
				// no other alnum words are ok
				if ( wids[a] ) break;
				// no tids
				if ( tids[a] ) {
					// anchor tag is ok though.
					// fixes mrmovies.com's 
					// "Mon|Tue|..." menu thing
					if ( !isBreakingTagId(tids[a]) )
						continue;
					// fix dj johnny b, has list of
					// month/daynum/years one per line
					///if ( skipBreakingTags ) continue;
					// now allow any tag if di and dj are
					// in the same sentence!
					//if(sp[dj->m_a]->m_sentenceSection == 
					//   sp[di->m_a]->m_sentenceSection ) 
					//	continue;
					// break the list
					if ( ! ignoreBreakingTags ) break;

					// do not string together
					// "monday" "tuesday" ... when they
					// are in a table heading row that
					// is for a weekly schedule!!!! let's
					// apply this to all date types
					// though in case they are indeed
					// headers in a table row.
					if( //di->m_type == DT_DOW &&
					    //dj->m_type == DT_DOW &&
					   ((tids[a] & BACKBITCOMP)==TAG_TD ||
					    (tids[a] & BACKBITCOMP)==TAG_TH )&&
					   di->m_section &&
					   dj->m_section &&
					   di->m_section->m_tableSec ==
					   dj->m_section->m_tableSec &&
					   di->m_section->m_rowNum >= 1 &&
					   di->m_section->m_rowNum == 
					   dj->m_section->m_rowNum )
						break;

					// otherwise, do not break it unless
					// the list item shares the line with
					// other text... like how we check
					// in makeCompounds()
					int32_t k;

					/*
					// scan to left of di
					k = di->m_a - 1;
					bool gotBreak1 = false;
					// need a breaking tag to follow
					for ( ; k >= 0 ; k-- ) {
						QUICKPOLL(m_niceness);
						if ( wids[k] ) break;
						if ( ! tids[k] ) continue;
						if (!isBreakingTagId(tids[k]))
							continue;
						gotBreak1 = true;
						break;
					}
					// need a breaking tag after it before
					// hitting another alnum word in order
					// for it to be isolated
					if ( ! gotBreak1 ) break;
					*/

					// same for dj, but check word after it
					k = dj->m_b;
					bool gotBreak2 = false;
					// need a breaking tag to follow
					for ( ; k < m_nw ; k++ ) {
						QUICKPOLL(m_niceness);
						// if we hit another date
						// consider that like a 
						// breaking tag to fix unm.edu
						if ( dx && k == dx->m_a ) {
							gotBreak2 = true;
							break;
						}
						if ( wids[k] ) break;
						if ( ! tids[k] ) continue;
						if (!isBreakingTagId(tids[k]))
							continue;
						gotBreak2 = true;
						break;
					}
					// need a breaking tag after it before
					// hitting another alnum word in order
					// for it to be isolated
					if ( ! gotBreak2 ) 
						break;
					// otherwise, they are both their
					// own line so let them bond.
					continue;
				}
				// allow any punct now to fix unm.edu which
				// uses a ';' between
				// "Tues., Wed., Thurs., 12 noon - 5 pm" and
				// "Fri. 9 am. - 2:30 pm"
				// i think this is even weaker than allowing
				// breaking tags in, so we should be ok.
				if ( ignoreBreakingTags ) {
					// get sentence containing di
					Section *n1 = sp[di->m_a];
					Section *n2 = sp[dj->m_a];
					n1 = n1->m_sentenceSection;
					n2 = n2->m_sentenceSection;
					// if either not in sentence, forget it
					// Happens if in javascript i guess
					// and we got no sections
					if ( ! n1 ) break;
					if ( ! n2 ) break;
					// get sections
					Section *s1 = sp[di->m_a];
					Section *s2 = sp[dj->m_a];
					// blow up
					for ( ; s1 ; s1 = s1->m_parent ) {
						if ( ! s1->m_parent ) break;
						if (s1->m_parent->contains(s2))
							break;
					}
					for ( ; s2 ; s2 = s2->m_parent ) {
						if ( ! s2->m_parent ) break;
						if (s2->m_parent->contains(s1))
							break;
					}
					// crap, this breaks cabq.gov libraries
					// page because they often have
					// the Sunday hours in its own <p> tag!
					// so i commented it out.
					//
					// also the oppenheimer zvents.com url
					// now blends the Sep date block
					// with the Oct date block into one
					// list again.
					//
					// BUT combines "... First Thursdays"
					// with "Friday and Saturday" for
					// rialtopool.com which is bad. so
					// use an AND operator on these.
					//
					// if *BOTH* contain additional
					// sentences then i wouldn't connect
					// them together
					if(s1->m_alnumPosA!=n1->m_alnumPosA&&
					   s2->m_alnumPosB!=n2->m_alnumPosB )
						break;
					// ok, connect them together!
					continue;
				}

				// scan the punct word otherwise
				char *p    = wptrs[a];
				char *pend = p + wlens[a];
				for ( ; p < pend ; p += getUtf8CharSize(p) ) {
					// space is ok
					if ( *p == ' ' ) continue;
					// other whitespace is ok
					if ( is_wspace_utf8(p) ) continue;
					// comma is ok
					if ( *p == ',' ) continue;
					// "Mon|Tue|..." menu for mrmovies.com
					// so it being a header does not
					// hurt us!
					if ( *p == '|' ) continue;
					// "Fri. + Sat. until Midnight"
					if ( *p == '.' ) continue;
					if ( *p == '+' ) continue;
					// Fridays & Saturdays
					if ( *p == '&' ) continue;
					// but fix "24/7" for guynndollsllc.com
					// page4.html
					if ( *p=='/' && di->m_type==DT_DAYNUM )
						break;
					// panjea.org has "... guest instructor
					// in March/April..."
					if ( *p == '/' ) continue;
					// otherwise stop
					break;
				}
				// continue if ok
				if ( p >= pend ) continue;
				// otherwise, stop
				break;
			}
			// if allowable junk between di and dj, add day to list
			if ( a < dj->m_a ) break;
			// must be ascending order!
			//if ( np > 0 && ptrs[np-1]->m_num >= dj->m_num ) {
			//	// that is a deal killer
			//	np = 0;
			//	// stop
			//	break;
			//}
			// fix t & t or m & m because those are not true dows!
			// also fix like m w wells too 
			if ( wlens[di->m_a] == 1 && 
			     wlens[dj->m_a] == 1 &&
			     di->m_type == DT_DOW &&
			     dj->m_type == DT_DOW ) 
				break;
			// get the type
			datetype_t tt ;//= DT_LIST;
			// and the subtype
			if ( di->m_type == DT_DAYNUM ) 
				tt = DT_LIST_DAYNUM;
			else if ( di->m_type == DT_MONTH )
				tt = DT_LIST_MONTH;
			else if ( di->m_type == (DT_DAYNUM|DT_MONTH) )
				tt = DT_LIST_MONTHDAY;
			else if ( di->m_type == DT_TOD ) 
				tt = DT_LIST_TOD;
			else if ( di->m_type == DT_DOW )
				tt = DT_LIST_DOW;
			else
				tt = DT_LIST_OTHER;

			// record if we had am/pm
			if ( dj->m_type==DT_TOD && 
			     (dj->m_suppFlags & SF_HAD_AMPM) )
				// was it am or pm
				lastHadAMPM = true;

			// . fix for mrmovietimes.com
			// . "10:20am, 5:10" (list of tods, only "am" is given)
			// . force it to pm for addIntervals() function
			if ( dj->m_type == DT_TOD && 
			     !(dj->m_suppFlags & SF_HAD_AMPM) &&
			     lastHadAMPM )
				dj->m_suppFlags |= SF_PM_BY_LIST;
			
			// make it
			if ( ! DD ) DD = addDate(tt,0,di->m_a,di->m_b,0);
			// return false on error
			if ( ! DD ) return false;
			// start it
			if ( DD->m_numPtrs == 0 ) DD->addPtr ( di , i , this );
			// . add to our list of things
			// . this NULLs out anything we add to it!
			DD->addPtr ( dj , j , this );
			// record last
			//last = dj;
			// point to next date atom
			//j++;
			// and switch
			di = dj;
			// stop if too many!
			if ( DD->m_numPtrs >= 100 ) break;
		}
		// must have at least TWO things to be a list
		if ( np <= 1 ) continue;
		// sanity check
		if ( DD->m_numPtrs > 100 ) { char *xx=NULL;*xx=0; }
		// advance i over the list we just made
		i = j - 1;
	}
	return true;
}

// if monthDayOnly is true then we want to combine Month and Day date types so
// that addLists() can fix "Dec 11, 18 Jan 1" by making sure that is a list 
bool Dates::makeCompounds ( Words *words , 
			    bool monthDayOnly ,
			    bool linkDatesInSameSentence ,
			    //bool dowTodOnly ,
			    bool ignoreBreakingTags ) {

	char       **wptrs = words->getWords    ();
	int32_t        *wlens = words->getWordLens ();
	int64_t   *wids  = words->getWordIds  ();
	nodeid_t    *tids  = words->getTagIds   ();

	// this range algo only works on simple date types for now
	datetype_t simpleFlags = DT_TOD|DT_DOW|DT_DAYNUM|DT_MONTH|DT_YEAR;

	datetype_t specialTypes = 
		DT_HOLIDAY      | // thanksgiving
		DT_SUBDAY       | // mornings
		DT_SUBWEEK      | // weekends
		DT_SUBMONTH     | // last day of month
		DT_EVERY_DAY    | // 7 days a week
		DT_SEASON       | // summers
		DT_ALL_HOLIDAYS ; // "holidays"

	// int16_tcut
	Section **sp = NULL;
	//if ( linkDatesInSameSentence ) sp = m_sections->m_sectionPtrs;
	if ( m_sections ) sp = m_sections->m_sectionPtrs;

	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if in url
		if ( ! ( di->m_flags & DF_FROM_BODY ) ) continue;
		// must be a simple month
		if ( monthDayOnly && di->m_type != DT_MONTH ) continue;
		// and non-numeric
		if ( monthDayOnly && di->m_flags & DF_MONTH_NUMERIC ) continue;
		// isolated daynums can never start a compound.
		// fixes "1st & 3rd Saturdays 7:30-10:30PM" for abqfolkfest
		if ( di->m_hasType == DT_DAYNUM           ) continue;
		if ( di->m_hasType == (DT_LIST_DAYNUM|DT_DAYNUM) ) continue;
		// never make a list of calendar daynums
		if ( di->m_type == DT_DAYNUM && (di->m_flags & DF_IN_CALENDAR))
			continue;
		// reset
		datetype_t lastType = di->m_hasType;//0
		// count them
		//int32_t np = 0;
		// init j loop
		int32_t j = i + 1;
		// int16_tcut
		//Date *DD = NULL;
		// mark as gotten
		datetype_t got = di->m_hasType;
		// make a list of ptrs
		Date *ptrs[100];
		int32_t  index[100];
		int32_t np = 0;
		// add di
		ptrs[np] = di;
		// save index
		index[np] = i;
		// advance
		np++;
		// mark it
		Date *prev  = di;
		Date *lastNonNull = di;
		// declare up here
		Date *dj;
		// loop over all dates starting with this word number
		for ( ; j < m_numDatePtrs ; j++ , prev = lastNonNull ) {
			// get it from that
			dj = m_datePtrs[j];
			// skip if ignored, part of a range or list?
			if ( ! dj ) continue;
			// skip if in url
			if ( ! ( dj->m_flags & DF_FROM_BODY ) ) continue;

			// never make a list with calendar daynums
			if ( dj->m_type == DT_DAYNUM && 
			     (dj->m_flags & DF_IN_CALENDAR))
				break;

			// update it
			lastNonNull = dj;

			// only one holiday!
			// stop "Thanksgiving, Christmas"
			// no, why? i can see if we have two other compounds
			// dates that give store hours for the weekdays and
			// weekends respectively, you might need to telescope
			// the christmas holiday to a different header than
			// the thanksgiving holiday...
			if ( (prev->m_type & specialTypes ) &&
			     (dj->m_type   & specialTypes ) )
				break;
			// isn't this what you meant:
			if ( (got & specialTypes ) &&
			     (dj->m_type & specialTypes ) )
				break;

			// fix "[Ages] 12-18" being a tod daynum compound where
			// we though 12 was a tod and 18 was a daynum for
			// meetup.com
			if ( prev->m_hasType == DT_TOD &&
			     np == 1 && // ! DD &&
			     !(prev->m_suppFlags & SF_HAD_AMPM) &&
			     dj->m_hasType == DT_DAYNUM )
				break;

			// must be a daynum?
			if ( monthDayOnly && ! ( dj->m_hasType & DT_DAYNUM ) )
				//continue;
				// this was causing "may" in 
				// "may ..... march 3-4" to make may pair
				// up with 3-4 so we need to break!
				break;

			/*
			if ( dowTodOnly ) {
				// one and only one must have dow or tod
				if ( (di->m_hasType & DT_DOW) &&
				     (dj->m_hasType & DT_DOW) )
					break;
				if ( (di->m_hasType & DT_TOD) &&
				     (dj->m_hasType & DT_TOD) )
					break;
				if ( !(di->m_hasType & DT_DOW) &&
				     !(dj->m_hasType & DT_DOW) )
					break;
				if ( !(di->m_hasType & DT_TOD) &&
				     !(dj->m_hasType & DT_TOD) )
					break;
			}
			*/

			// in different sentence and we are not ignoring
			// breaking tags, forget it! basically breaking tags
			// and different sentences should be treat as 
			// equivalent
			//bool sameSent = true;
			//if ( sp )
			//	sameSent = ( sp[di->m_a]->m_senta ==
			//		     sp[dj->m_a]->m_senta );
			//if ( ! ignoreBreakingTags && ! sameSent )
			//	break;

			// stop if we already had this date type in sequence
			bool stop = false;
			if ( (dj->m_hasType & simpleFlags) & 
			     (got           & simpleFlags) ) 
				stop = true;
			// . allow back to back lists of days of month though
			// . i.e. dec 1,2-4 and jan 3-5 2009
			if ( dj->m_hasType == DT_DAYNUM &&
			     lastType      == DT_DAYNUM )
				// allow it through, its a list!
				stop = false;
			// stop if a type we already got
			if ( stop ) break;
			// set this
			lastType = dj->m_hasType;

			// get word range
			int32_t a = prev->m_b;
			int32_t b = dj->m_a;

			// can't include a date in the url now
			if ( prev->m_flags & DF_FROM_URL ) continue;
			if ( dj  ->m_flags & DF_FROM_URL ) continue;
			if ( a < 0 ) continue;
			if ( b < 0 ) continue;

			// allow pairing up across a br tag if there was a ':'
			bool hadColon = false;

			bool sameSentLink = false;
			// if in same sentence, always link them i guess
			if ( linkDatesInSameSentence ) {
				// assume so
				sameSentLink = true;
				// mus tbe in same sentence
				if (sp[di->m_a]->m_senta!=sp[dj->m_a]->m_senta)
					sameSentLink = false;
				// these are split sentences? whadup?
				if ( sp[di->m_a]->m_senta < 0 ) 
					sameSentLink = false;
				// ignore fuzzies if not already linked
				if ( di->m_flags & DF_FUZZY ) 
					sameSentLink = false;
				// ignore fuzzies if not already linked
				if ( dj->m_flags & DF_FUZZY ) 
					sameSentLink = false;
				// . right now only support linking of a tod
				//   or tod range to a dow
				// . fixes "The Saturday market is open from 
				//   10 a.m.-3 p.m" for santafe.org
				// . if the date types are too complicated it
				//   like "every wed" and "every friday" for
				//   hardwoodmuseum.org it fails...
				/*
				bool ok = false;
				if ( di->m_hasType == DT_DOW &&
				     dj->m_hasType == (DT_TOD|DT_RANGE_TOD))
					ok = true;
				if ( di->m_hasType == DT_DOW &&
				     dj->m_hasType == DT_TOD )
					ok = true;
				if ( dj->m_hasType == DT_DOW &&
				     di->m_hasType == (DT_TOD|DT_RANGE_TOD))
					ok = true;
				if ( dj->m_hasType == DT_DOW &&
				     di->m_hasType == DT_TOD )
					ok = true;
				// must be ok types
				if ( ! ok ) continue;
				*/
				// otherwise, instantly link them
				//a = b;
				// no! because "closed" could separate them
				// and we aren't allowed to link over that
				// word!
				//sameSentLink = true;
			}

			// assume not "onoing"
			//bool ongoing = false;
			// see if they belong together
			for ( ; a < b ; a++ ) {
				// breathe
				QUICKPOLL(m_niceness);
				// no breaking tags can be in sequence
				if ( tids[a] ) {
					// always ignore now!!
					//continue;
					// if already linked, skip
					if ( sameSentLink ) continue;
					// ok if not breaking tag
					if ( ! isBreakingTagId(tids[a]) )
						continue;
					// . mcachicago.org uses <time> tags
					// . TODO: treat as breaking if rss
					if ((tids[a]&BACKBITCOMP)==
					    TAG_XMLTAG )
						continue;
					// allow for br tags since they
					// are often used as line breaks.
					// wow, this causes major issues with
					// like 15 urls when i uncomment it!
					if ( tids[a] == TAG_BR && hadColon )
						continue;

					if ( ! ignoreBreakingTags ) 
						break;

					// get sentence containing di
					Section *n1 = sp[di->m_a];
					Section *n2 = sp[dj->m_a];
					n1 = n1->m_sentenceSection;
					n2 = n2->m_sentenceSection;
					// if either not in sentence, forget it
					// Happens if in javascript i guess
					// and we got no sections
					if ( ! n1 ) break;
					if ( ! n2 ) break;
					// get sections
					Section *s1 = sp[di->m_a];
					Section *s2 = sp[dj->m_a];
					// blow up
					for ( ; s1 ; s1 = s1->m_parent ) {
						if ( ! s1->m_parent ) break;
						if (s1->m_parent->contains(s2))
							break;
					}
					for ( ; s2 ; s2 = s2->m_parent ) {
						if ( ! s2->m_parent ) break;
						if (s2->m_parent->contains(s1))
							break;
					}
					// if either contains additional
					// sentences then i wouldn't connect
					// them together
					if ( s1->m_alnumPosA!=n1->m_alnumPosA )
						break;
					if ( s2->m_alnumPosA!=n2->m_alnumPosA )
						break;
					     
					// allow for <td> or </td> since
					// they often put store hours in
					// a table with the dow in left column
					// and the tod range in the right 
					// column. crap they also use
					// tr and div... so let all breaking
					// tags through now...
					// unless either dj or di is
					// in a sentence with other text!!!
					// so get each sentence...

					// scan to left of di
					int32_t k = di->m_a - 1;
					// backup over punct word
					if ( k >= 0 && ! wids[k] && ! tids[k] )
						k--;
					// is it a tag or word? if its a word
					// assume we are part of a sentence
					// and should not pair up with dj
					if ( wids[k] ) break;
					// same for dj, but check word after it
					k = dj->m_b;
					// skip over punct word
					if ( k < m_nw && ! wids[k] && !tids[k])
						k++;
					// is it a tag or word? if its a word
					// assume we are part of a sentence
					// and should not pair up with di
					if ( wids[k] ) break;
					// otherwise, they are both their
					// own sentence so let them bond.
					continue;
					// this was originally intended to fix
					// reverbnation.com's 
					// "<div>28</div><div>Jan</div>" but
					// we actually put that fix in the
					// "01 nov" canonical date detection
					// loop above. so this is here without
					// any reason, so i will comment it
					// out now
					/*
					// "p1" = "breaking section"
					Section *p1 = sp[a];
					// if contains more than just the
					// previous date, forget it
					if ( p1->m_firstWordPos!= prev->m_a )
						break;
					if ( p1->m_lastWordPos != prev->m_b-1 )
						break;
					// same must be true for next date
					Section *p2 = sp[b];
					if ( p2->m_firstWordPos!= dj->m_a )
						break;
					if ( p2->m_lastWordPos != dj->m_b-1 )
						break;
					// stop if like <br> or </div> 
					//if(isBreakingTagId(m_tids[a]))break;
					// its an ok tag
					continue;
					*/
					// ... and go back to our old algo...
					break;
				}
				// punct?
				if ( ! wids[a] ) { 
					// if already linked, skip
					if ( sameSentLink ) continue;
					//char hadHyphen = 0;
					char *p    = wptrs[a];
					char *pend = p + wlens[a];
					bool stop = false;
					for ( ; p < pend ; p++ ) {
						if ( *p == ':' ) {
							hadColon = 1;
							break;
						}
						//if ( *p == '-' ) {
						//	hadHyphen = 1;
						//}
						// could indicate end of
						// sentence. stay within
						// our sentence until after
						// we know what the sentences
						// are... in which case
						// m_sections will be set
						//if ( ! m_sections &&
						//     ( *p == '.' ||
						//       *p == '!' ||
						//       *p == '?' ) ) {
						//	stop = true;
						//	break;
						//}
					}
					// stop now
					if ( stop ) break;
					// fix
					//Friday, November 4, 2011 at 11:00 AM-
					//Sunday, November 6, 2011 at 10:00 PM
					// so we don't pair up
					// 11:00AM with Sunday and fuck up the
					// range for floridaflowfest-ehometext.
					// eventbrite.com/
					// BUT this breaks"Tuesday thru Friday 
					// - 8:30am to 4:30pm" for zvents.com
					// kimo theater office hrs.
					//if ( hadHyphen && dowTodOnly )
					//	break;
					// any tag following a colon means
					// to break the sentence. so following
					// that logic that's in Sections::
					// addSentences() we should break
					// to! we have stuff like 
					// on all saturdays from 10am-3pm:<br>
					// ... list of doms.
					//if ( a+1<m_nw && tids&&tids[a+1] )
					//	break;
					//if ( a-1>=0 && tids&&tids[a-1] )
					//	break;
					// try stopping on colons
					// . no, makes villr.com and 
					//   texasdrums.drums.org fail 
					//   because the date near the colon
					//   ends up getting telescoped to
					//   by another date which is wrong...
					//if ( hadColon ) break;
					continue;
				}
				//if ( linkDatesInSameSentence ) continue;
				// allow "at" like "Dec 11th at 8pm"
				if ( wids[a] == h_at ) continue;
				// allow "on" like "8pm on Dec 11th"
				if ( wids[a] == h_on ) continue;
				// May 1, 2009 from 5pm to 8pm
				//if ( wids[a] == h_from ) continue;
				// "friday the 27th of november"
				if ( wids[a] == h_of ) continue;
				if ( wids[a] == h_the ) continue;
				// "Tuesday from 3:00pm until 7:30pm"
				if ( wids[a] == h_from ) continue;
				// "Tuesday evening at 7:30"
				if ( wids[a] == h_evening ) continue;
				if ( wids[a] == h_night ) continue;
				if ( wids[a] == h_morning ) continue;
				if ( wids[a] == h_afternoon ) continue;
				if ( wids[a] == h_evenings ) continue;
				if ( wids[a] == h_nights ) continue;
				if ( wids[a] == h_mornings ) continue;
				if ( wids[a] == h_afternoons ) continue;

				// <sup> is ok now "Nov 4<sup>th</sup>"
				if ( wids[a] == h_st ) continue;
				if ( wids[a] == h_nd ) continue;
				if ( wids[a] == h_rd ) continue;
				if ( wids[a] == h_th ) continue;

				// fix activedatax.com:
				// "start date: 10/7/2011 start time: 6:00pm
				//  end date: 10/7/2011 end time: 11:59pm"
				if ( wids[a] == h_start  ) continue; 
				if ( wids[a] == h_end    ) continue; 
				if ( wids[a] == h_starts ) continue; 
				if ( wids[a] == h_ends   ) continue; 
				if ( wids[a] == h_time   ) continue; 


				// even if in same sentence and should be
				// linked, the word "closed" will break 
				// that up. this fixes 
				// "Tues evenings 5-8 closed weekends" which
				// was not finding the word "closed" and not
				// setting DF_CLOSE_DATE for "weekends"
				if ( wids[a] == h_closed ) break;
				if ( wids[a] == h_closes ) break;
				if ( wids[a] == h_closure) break;
				if ( wids[a] == h_except ) break;

				// if already linked, skip
				if ( sameSentLink ) continue;

				// 
				// . i guess we allow any words for now!
				// . no! let lets the telscoper and
				//   addRanges() and addLists() do this now
				// . those algos could also check for ambiguity
				//   and not pair things up if there is some
				//if ( wids[a] ) continue;
				// . could be an open ended range
				// . in that case, when done, we set the
				//   m_min* members to the spideredTime for all
				//   dates after word #a.
				/*
				if ( m_wids[a] == h_until   ||
				     m_wids[a] == h_through ||
				     m_wids[a] == h_thru    ) {
					ongoing = true;
					continue;
				}
				*/
				if ( wids[a] ) break;
				// any punct ok for now
				continue;
			}
			// disrupted?
			if ( a < b && j != i ) break;

			// . if prev is a month/daynum compound, then nuke it!
			//   that is way too fuzzy!
			// . this fixes "2005 -22" where 2005 was part of a 
			//   phone # and 22 was part of a street address for
			//   dexknows.com
			if ( np == 1 && // ! DD &&
			     prev->m_type == DT_YEAR && 
			     dj->m_type == DT_DAYNUM )
				break;
			// likewise, the otherway is bad too!
			if ( np == 1 && // ! DD &&
			     dj->m_type == DT_YEAR && 
			     prev->m_type == DT_DAYNUM )
				break;
			// add it
			ptrs[np] = dj;
			// save index
			index[np] = j;
			// advance
			np++;
			// flags or
			got |= dj->m_hasType;
			// full?
			if ( np >= 100 ) break;
			/*
			// use this now
			if ( ! DD ) {
				// make first date
				DD = addDate(DT_COMPOUND,0,di->m_a,0,0);
				// return false on error
				if ( ! DD ) return false;
				// add in date ptr #i as first ptr
				DD->addPtr ( di , i , this );
			}
			// add in flag
			//if ( ongoing ) DD->m_flags |= DF_ONGOING;
			//if ( ongoing ) dj->m_flags |= DF_ONGOING;
			// set new end point MDW LEFT OFF HERE
			DD->m_b = dj->m_b;
			// flags or
			got |= dj->m_hasType;
			// add it in
			DD->addPtr ( dj , j , this );
			// swap for next guy
			di = dj;
			// we only reserved mem for 100 Date::m_ptrs[]!
			if ( DD->m_numPtrs >= 100 ) break;
			*/
		}
		// need at least 2 to tango
		if ( np < 2 ) continue;

		// . dates that end in just a month are bad!
		// . fix "6-8 may" for gmsstrings.com/default.aspx
		// . "grades 6-8 may learn to play..."
		// . do not add this compound date if it ends in a simple month
		// . crap, then that breaks "Saturday before Second Sunday"
		//   compounding up with "of February"
		if ( ptrs[np-2]->m_type == DT_RANGE_DAYNUM &&
		     ptrs[np-1]->m_type == DT_MONTH ) 
			continue;

		// init DD
		Date *DD = addDate(DT_COMPOUND,0,di->m_a,0,0);
		// return false on error
		if ( ! DD ) return false;

		// ok, now make the compound date from the list
		for ( int32_t j = 0 ; j < np ; j++ ) {
			// update
			DD->addPtr ( ptrs[j] , index[j] , this );
			// update m_b
			DD->m_b = ptrs[j]->m_b;
		}

	}
	return true;
}

// . sets Date::m_dateHash
// . if date represents the exact same times then should have same date hash
// . normalizes
// . i.e. "11/12/11 = Nov 12th 2011" or "11:00am = 11 in the morning"
void Dates::setDateHashes ( ) {
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// do recursively
		getDateHash ( di , di );
		// . sanity check
		// . happened for "6 5 5 6 6 5" string of daynums?
		if ( di->m_dateHash64 == 0LL ) di->m_dateHash64 = 999;
	}
}

uint64_t Dates::getDateHash ( Date *di , Date *orig ) {
	uint64_t dh = getDateHash2 ( di , orig );
	di->m_dateHash64 = dh;
	return dh;
}

// ultimately this is the same problem we have with normalizing the
// english format of a date (printTextNorm())
uint64_t Dates::getDateHash2 ( Date *di , Date *orig ) {

	if ( di->m_type & DT_RANGE_ANY ) {
		uint64_t h1 = getDateHash ( di->m_ptrs[0] , orig );
		uint64_t h2 = getDateHash ( di->m_ptrs[1] , orig );
		return hash64h ( h1 , h2 );
	}
	if ( (di->m_type & DT_COMPOUND) ||
	     (di->m_type & DT_LIST_ANY) ||
	     (di->m_type & DT_TELESCOPE) ) {
		// blank it out
		uint64_t h = 0;
		//uint64_t lasttt = 0;
		// loop over ptrs
		for ( int32_t i = 0 ; i < di->m_numPtrs ; i++ ) {
			uint64_t tt = getDateHash ( di->m_ptrs[i] , orig );
			h = hash32h ( tt, h );
			// . watch out for lists of just the same thing!
			// . fix "5.5" list of daynums for mytravelguide.com
			//if ( tt == lasttt ) continue;
			//lasttt = tt;
			// or 1,2,1,2 would ultimately be zero!
			//if ( (h ^ tt) != 0LL ) h ^= tt;
		}
		return h;
	}

	// "Friday Evening 7pm" should ignore "evening"
	if ( (orig->m_hasType & DT_TOD) && di->m_type == DT_SUBDAY )
		return 0;
	if ( (orig->m_hasType & DT_DAYNUM) && 
	     // fix for "July 20-21, 2011 [[]] Wednesday [[]] 8:00 a.m"
	     // for illinois.edu
	     !(orig->m_hasType & DT_RANGE_DAYNUM) &&
	     di->m_type == DT_DOW )
		return 0;
	if ( (orig->m_hasType & DT_COMPOUND) && di->m_type == DT_YEAR )
		return 0;

	uint64_t dt = di->m_type;

	if ( dt == DT_MONTH ) return hash64h(dt,di->m_num);
	if ( dt == DT_DAYNUM )return hash64h(dt,di->m_num);
	if ( dt == DT_DOW ) {
		uint64_t h = hash64h(dt,(uint64_t)di->m_dowBits);
		// combine with "last" "3rd" etc. for "3rd thursday"
		suppflags_t rmask = 
			SF_FIRST|
			SF_LAST|
			SF_SECOND|
			SF_THIRD|
			SF_FOURTH|
			SF_FIFTH;
		suppflags_t sf = di->m_suppFlags & rmask;
		if ( sf ) h = hash64h ((int64_t)sf,h);
		return h;
	}
	if ( dt == DT_TOD ) return hash64h(dt,di->m_num);
	if ( dt == DT_YEAR ) return hash64h(dt,di->m_num);
	if ( dt == DT_HOLIDAY ) return hash64h(dt,di->m_num);
	if ( dt == DT_TIMESTAMP ) return hash64h(dt,di->m_num);
	// afternoon, morning etc. (TODO: fix m_num)
	if ( dt == DT_SUBDAY ) return hash64h(dt,di->m_num);
	// weekends, etc (TODO: fix m_num)
	if ( dt == DT_SUBWEEK ) return hash64h(dt,di->m_num);
	// last day of the month (TODO: fix m_num)
	if ( dt == DT_SUBMONTH ) return hash64h(dt,di->m_num);
	// every day
	if ( dt == DT_EVERY_DAY ) return hash64h(dt,di->m_num);
	// summer winter, etc.
	if ( dt == DT_SEASON ) return hash64h(dt,di->m_num);
	// holidays
	if ( dt == DT_ALL_HOLIDAYS ) return hash64h(dt,di->m_num);
	// wtf?
	char *xx=NULL;*xx=0;
	return 0;
}



int32_t Dates::printDateNeighborhood ( Date *di , Words *w ) {
	int32_t           nw = w->getNumWords();
	char      **wptrs = w->getWords   ();
	int32_t       *wlens = w->getWordLens();
	nodeid_t    *tids = w->m_tagIds;
	int64_t   *wids = w->getWordIds();
	SafeBuf sb;
	int32_t a = di->m_a;
	int32_t b = di->m_b;
	if ( a < 0 ) return 0;
	a -= 10;
	b += 10;
	if ( a < 0 ) a = 0;
	if ( b > nw ) b = nw;
	bool lastWasSpace = false;
	for ( int32_t i = a ; i < b ; i++ ) {
		if ( i == di->m_a )
			sb.pushChar('*');
		if ( i == di->m_b )
			sb.pushChar('*');
		if ( tids[i] ) {
			if ( !lastWasSpace ) sb.pushChar(' ');
			lastWasSpace = 1;
			continue;
		}
		if ( ! wids[i] ) {
			if ( !lastWasSpace ) sb.pushChar(' ');
			lastWasSpace = 1;
			continue;
		}
		lastWasSpace = false;
		sb.safeMemcpy ( wptrs[i],wlens[i] );
	}
	// print out
	sb.pushChar('\0');
	char *s = sb.getBufStart();
	log("neigh: %s\n",s);
	return 0;
}

// for gdb to call
int32_t Dates::printDates2 ( ) { 
	printDates ( NULL ); 
	return 1; 
}

int32_t Dates::print ( Date *d ) {
	SafeBuf sb;
	d->printText ( &sb , m_words , false);
	fprintf(stderr,"%s\n",sb.getBufStart() );
	return 1;
}

// make an array of the Date ptrs that are in a date such that each ptr
// does not consist of any other ptrs, but is atomic
Date **Dates::getDateElements ( Date *di , int32_t *ne ) {
	// already did it?
	if ( di->m_numFlatPtrs > 0 ) {
		*ne = di->m_numFlatPtrs;
		return (Date **)(m_cbuf.getBufStart()+di->m_flatPtrsBufOffset);
	}
	// use cbuf for this
	if ( ! m_cbuf.reserve ( 20*sizeof(Date *) ) ) return NULL;
	// int16_tcut
	int32_t startOffset = m_cbuf.length();
	// store here
	di->m_flatPtrsBufOffset = startOffset;
	// . store all ptrs into there
	// . it returns NULL with g_errno set on error
	if ( ! addPtrToArray ( di ) ) return NULL;
	// get the ending offset after adding the date ptrs
	int32_t endOffset = m_cbuf.length();
	// set length
	*ne = (endOffset - startOffset)/sizeof(Date *);
	// set that
	di->m_numFlatPtrs = *ne;
	// must be > 0 
	if ( *ne <= 0 ) { char *xx=NULL;*xx=0; }
	// point to the buffer
	Date **p = (Date **)(m_cbuf.getBufStart() + startOffset);
	// sort it by Date::m_a so Events::makeEventDisplay2() works right
 bubbleSortLoop:
	char flag = 0;
	for ( int32_t i = 1 ; i < *ne ; i++ ) {
		if ( p[i]->m_a >= p[i-1]->m_a ) continue;
		Date *tmp = p[i-1];
		p[i-1] = p[i];
		p[i]   = tmp;
		flag   = 1;
	}
	if ( flag ) goto bubbleSortLoop;
	// return ptr to array of ptrs
	return (Date **)(m_cbuf.getBufStart()+di->m_flatPtrsBufOffset);
}

bool Dates::addPtrToArray ( Date *dp ) {
	// only add base types
	if ( dp->m_numPtrs == 0 ) {
		if ( ! m_cbuf.pushPtr(dp) ) return false;
		return true;
	}
	// recursive otherwise
	for ( int32_t i = 0 ; i < dp->m_numPtrs ; i++ )
		if ( ! addPtrToArray ( dp->m_ptrs[i] ) )
			return false;
	return true;
}



bool Dates::printDates ( SafeBuf *sbArg ) {

	SafeBuf *sb = sbArg;

	SafeBuf tmp;
	// skip if not debug
	if ( ! sbArg ) sb = &tmp;

	char *format = "unknown";
	if ( m_dateFormat == DF_AMERICAN ) format = "american";
	if ( m_dateFormat == DF_EUROPEAN ) format = "european";

	// int16_tcut 
	//Sections *ss = m_sections;

	char *bh = "";
	if ( m_badHtml ) bh = " (<font color=red><b>bad html</b></font>)";

	if ( sbArg )
		sb->safePrintf("<table width=100%% border=1 cellpadding=4>"
			       "<tr><td colspan=20><b>"
			       "Dates</b>"
			       " (format=%s) (firstgood=%"INT32" lastgood=%"INT32")"
			       " 25hrRespider=%"INT32""
			       "(sitehash=0x%"XINT32")%s"
			       "</td></tr>\n"
			       "<tr><td>#</td>"
			       "<td>startWord</td>"
			       "<td>endWord</td>"
			       "<td>text</td>"
			       "<td>pub date score</td>"
			       "<td>timestamp</td>"
			       "<td>timezone</td>"
			       "<td>date content hash</td>"
			       "<td>turk tag hash</td>"
			       //"<td>sentId</td>"
			       "<td>flags</td>"
			       "<td>tagHash</td>"
			       "<td>occ#</td>"
			       "<td>clockHash</td>"
			       "<td>termid</td>"
			       "</tr>\n",
			       format,m_firstGood,m_lastGood,
			       (int32_t)m_needQuickRespider,m_siteHash,bh);
	else
		sb->safePrintf(
			       "Publication Date Candidates "
			       " (format=%s) (firstgood=%"INT32" lastgood=%"INT32")"
			       " 25hrRespider=%"INT32""
			       "(sitehash=0x%"XINT32")%s"
			       "\n"
			       "# | "
			       "startWord | "
			       "endWord | "
			       "text | "
			       "pub date score | "
			       "timestamp | "
			       "timezone | "
			       "flags | "
			       "tagHash | "
			       "occ# | "
			       "clockHash | "
			       "termid "
			       "\n",
			       format,m_firstGood,m_lastGood,
			       (int32_t)m_needQuickRespider,m_siteHash,bh);


	// dates from body
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if nuked from range logic above
		if ( ! di ) continue;
		// flag it
		di->m_flags |= DF_NOTKILLED;
		// print it
		di->print( sbArg, m_sections,m_words,m_siteHash,i,m_best,this);
	}


	// dates from body
	for ( int32_t i = 0 ; i < m_numTotalPtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_totalPtrs[i];
		// skip if nuked from range logic above
		if ( ! di ) continue;
		// only if killed
		if ( di->m_flags & DF_NOTKILLED ) continue;
		// print it
		di->print( sbArg, m_sections,m_words,m_siteHash,i,m_best,this);
	}

	if ( ! sbArg ) {
		fprintf(stdout,"%s",sb->getBufStart() );
		return true;
	}

	sb->safePrintf("</table>\n<br>" );

	sb->safePrintf("<i>NOTE: The publication date will be in bold. "
		       "The publication date has the highest score out of "
		       "all the Candidates. If the winning candidate has the "
		       "\"needmoreinfo\" flag set then no publication date "
		       "will be used, and the page will be scheduled to be "
		       "respidered 25 hours from now."
		       "</i><br><br>");

	sb->safePrintf("<i>NOTE: The date range in blue is the min pub "
		       "date, as determined by when the link first appeared "
		       "on its parent page. we assume any dates are relative "
		       "to the years in that time range in blue."
		       "</i><br><br>");
	return true;
}

// just print the date itself
void Date::printText ( SafeBuf *sb , Words *words , bool inHtml ) {
	nodeid_t *tids = words->getTagIds();
	char **wptrs = words->getWords();
	int32_t  *wlens = words->getWordLens ();
	int64_t   *wids = words->getWordIds();

	//if ( m_numPtrs == 0 && (m_flags & DF_CLOSE_DATE) )
	if ( (m_flags & DF_CLOSE_DATE) && inHtml )
		sb->safePrintf("<font color=red>");

	if ( m_numPtrs > 0 && m_mostUniqueDatePtr == m_ptrs[0] && inHtml )
		sb->safePrintf("<u>");

	bool lastWasBullet = false;
	bool lastWasSpace  = false;
	// print out each word
	for ( int32_t j = m_a ; j < m_b ; j++ ) {
		// skip if tag
		if ( tids[j] ) {
			// print a comma for breaking tags separating list elms
			if ( isBreakingTagId ( tids[j] ) && 
			     // fix for <time> tag for mcachicago.com
			     (tids[j]&BACKBITCOMP) != TAG_XMLTAG &&
			     // really needs to be a new line tag
			     (tids[j]&BACKBITCOMP) != TAG_TD &&
			     ! lastWasBullet ) {
				sb->safePrintf(" &bull; ");
				lastWasBullet = true;
				lastWasSpace  = true;
			}
			if ( ! lastWasSpace ) {
				sb->pushChar(' ');
				lastWasSpace = true;
			}
			continue;
		}
		// print it otherwise
		sb->safeMemcpy(wptrs[j],wlens[j]);
		if ( ! wids[j] ) continue;
		lastWasBullet = false;
		lastWasSpace  = false;
	}

	if ( m_numPtrs > 0 && m_mostUniqueDatePtr == m_ptrs[0] && inHtml )
		sb->safePrintf("</u>");

	//if ( m_numPtrs == 0 && (m_flags & DF_CLOSE_DATE) )
	if ( (m_flags & DF_CLOSE_DATE) && inHtml )
		sb->safePrintf("</font>");

	// telescope ptrs
	for ( int32_t i = 1 ; m_type==DT_TELESCOPE && i<m_numPtrs;i++) {
		// get next ptr
		Date *dp = m_ptrs[i];
		// print delim
		sb->safePrintf(" [[]] ");

		if ( (dp->m_flags & DF_CLOSE_DATE) && inHtml )
			sb->safePrintf("<font color=red>");

		if ( dp == m_mostUniqueDatePtr && inHtml)
			sb->safePrintf("<u>");

		// print out each word
		dp->printText ( sb , words, false );
		//for ( int32_t j = dp->m_a ; j < dp->m_b ; j++ ) {
		//	// skip if tag
		//	if ( tids[j] ) continue;
		//	// print it otherwise
		//	sb->safeMemcpy(wptrs[j],wlens[j]);
		//}

		if ( dp == m_mostUniqueDatePtr && inHtml )
			sb->safePrintf("</u>");

		if ( (dp->m_flags & DF_CLOSE_DATE) && inHtml )
			sb->safePrintf("</font>");

	}
	// end in assumed year
	if ( m_flags & DF_ASSUMED_YEAR ) {
		//int32_t t1 = m_minPubDate;
		time_t t1 = m_minStartFocus;
		time_t t2 = m_maxStartFocus;
		if ( inHtml ) sb->safePrintf("<font color=blue>");
		sb->safePrintf(" ** %s- ",ctime(&t1));
		sb->safePrintf("%s",ctime(&t2));
		if ( inHtml ) sb->safePrintf("</font>");
	}
}
/*
static void setGroupNumRecursive ( Date       *dp          ,
				   datetype_t *accMaskPtr  ,
				   int32_t       *groupNumPtr ) {
	// if any of our siblings is a repeat type we have to inc group
	if ( *accMaskPtr & dp->m_hasType ) {
		*accMaskPtr = 0;
		*groupNumPtr = *groupNumPtr + 1;
	}

	// set group #s on our brothers first before descending!!!
	for ( int32_t i = 0 ; i < dp->m_numPtrs ; i++ ) {
		// int16_tcut
		Date *di = dp->m_ptrs[i];
		// same?
		if ( di->m_hasType &  *accMaskPtr ) {
			*accMaskPtr = 0;
			*groupNumPtr = *groupNumPtr + 1;
		}
		// or them up
		*accMaskPtr |= di->m_hasType;
		// assign group #
		di->m_groupNum = *groupNumPtr;
	}

	// reset this i guess, the whole point of doing brothers first...
	*accMaskPtr = 0;

	// then descend into each one
	for ( int32_t i = 0 ; i < dp->m_numPtrs ; i++ ) {
		// int16_tcut
		Date *di = dp->m_ptrs[i];
		// reset?
		setGroupNumRecursive ( di, accMaskPtr, groupNumPtr );
	}
}
*/
static char *s_mnames[13] = {
	"January",
	"February",
	"March",
	"April",
	"May",
	"June",
	"July",
	"August",
	"September",
	"October",
	"November",
	"December",
	"January"
};

char *s_dnames[8] = {
	"Sunday",
	"Monday",
	"Tuesday",
	"Wednesday",
	"Thursday",
	"Friday",
	"Saturday"
};

char *getDOWName ( int32_t dow ) {
	if ( dow < 0 || dow > 6 ) { char *xx=NULL;*xx=0; }
	return s_dnames[dow];
}

char *getMonthName ( int32_t month ) {
	if ( month < 0 || month > 11 ) { char *xx=NULL;*xx=0; }
	return s_mnames[month];
}


bool printDOW ( SafeBuf *sb , Date *dp ) {
	int32_t mi = dp->m_dow;
	if ( mi <= 0 || mi >= 8 ) { char *xx=NULL;*xx=0; }
	char *ev = NULL;
	// get the full date
	Date *full = dp;
	for ( ; full->m_dateParent; full = full->m_dateParent );
	// plural?
	if ( dp->m_suppFlags & SF_RECURRING_DOW )
		ev = "every";
	// int16_tcut
	Date *parent = dp->m_dateParent;
	// fix for "10pm Monday NIGHTS" for zipscene. only the
	// NIGHTS had this set
	if ( parent && (parent->m_suppFlags & SF_RECURRING_DOW) )
		ev = "every ";
	// sometimes we have "Monday and Wednesday ... Nights"
	// so our parent is a dow list and HD_NIGHT is brother of that parent
	// like for nickhotel.com
	if ( full->m_suppFlags & SF_RECURRING_DOW )
		ev = "every ";
	// 9:15am to 10:15am every Monday and Wednesday
	if ( parent &&
	     parent->m_type == DT_LIST_DOW &&
	     parent->m_ptrs[0] == dp &&
	     (parent->m_suppFlags & SF_RECURRING_DOW) )
		ev = "every ";
	// if we are recurring dow in a list, do not repeat
	// fix "EVERY saturday and EVERY sunday"
	if ( parent &&
	     parent->m_type == DT_LIST_DOW &&		     
	     parent->m_ptrs[0] != dp )
		ev = NULL;
	// but not if we are in a range like "Thursday through Saturday"
	// because "every THursday through every Saturday" is wrong.
	if ( parent && parent->m_type == DT_RANGE_DOW )
		ev = NULL;
	// are we a clsoed date?
	bool closed = false;
	// or parent?
	Date *pp = dp;
	for ( ; pp ; pp = pp->m_dateParent ) 
		if ( pp->m_flags & DF_CLOSE_DATE ) closed = true;
	// print it
	if ( ev && ! closed && ! sb->safePrintf("every ") ) return false;
	// every 1st & 2nd thursday
	suppflags_t sfmask = 0;
	sfmask |= SF_FIRST;
	sfmask |= SF_SECOND;
	sfmask |= SF_THIRD;
	sfmask |= SF_FOURTH;
	sfmask |= SF_FIFTH;
	sfmask |= SF_LAST;
	// how many do we have?
	suppflags_t mybits = (uint32_t)(sfmask&dp->m_suppFlags);
	int32_t nsf = getNumBitsOn32 ( mybits );
	// count each bit that we have in the loop too
	int32_t nc  = 0;
	// loop over the bits
	for ( suppflags_t k = SF_FIRST ; k <= SF_LAST ; k <<= 1 ) {
		if ( ! (dp->m_suppFlags & k ) ) continue;
		char *sep;
		if      ( nc     == 0   ) sep = "";
		else if ( nc + 1 <  nsf ) sep = ", ";
		else if ( nc + 1 == nsf ) sep = " and ";
		else                      sep = "";
		char *str ;
		if ( k == SF_FIRST  ) str = "1st";
		if ( k == SF_SECOND ) str = "2nd";
		if ( k == SF_THIRD  ) str = "3rd";
		if ( k == SF_FOURTH ) str = "4th";
		if ( k == SF_FIFTH  ) str = "5th";
		if ( k == SF_LAST   ) str = "last";
		if ( ! sb->safePrintf("%s%s",sep,str)) return false;
		nc++; 
	}

	// print space after them, if any
	if ( nc ) {
		if ( ! sb->safePrintf(" ")) return false; 
	}
	// print dow name properly then
	if ( ! sb->safePrintf("%s",s_dnames[mi-1]) )
		return false;

	// if closed and recurring print 's' instead of every
	// print it
	if ( ev && closed && ! sb->safePrintf("s") ) return false;

	//if ( nc && ! sb->safePrintf(" of the month") ) 
	//	return false;
	return true;
}

	
static bool printTOD ( SafeBuf *sb , time_t ttt ) {
	// seconds since start of day
	int32_t relativeTod = ttt % 86400;
	// convert to minutes since start of day
	int32_t totalMins = relativeTod / 60;
	// hours since start of day
	int32_t hour = totalMins / 60;
	int32_t min  = totalMins % 60;

	// print noon to avoid ambiguity with midnight
	if ( hour == 0 && min == 0 )
		return sb->safePrintf("midnight");
	if ( hour == 24 && min == 0 )
		return sb->safePrintf("midnight");
	if ( hour == 12 && min == 0 )
		return sb->safePrintf("noon");

	char *ap = " am";
	if ( hour >= 12 ) {
		ap = " pm";
		hour -= 12;
	}
	// still greater than? 2am?
	if ( hour >= 12 ) {
		ap = " am";
		hour -=12;
	}
	// 0 is noon usually
	if ( hour == 0 ) hour = 12;

	if ( min != 0 ) 
		return sb->safePrintf("%"INT32":%02"INT32"%s",hour,min,ap);
	
	return sb->safePrintf("%"INT32"%s",hour,ap) ;
}

static bool printMonthDay ( SafeBuf *sb , int32_t month , int32_t dayNum ) {
	// print that out
	char *suffix = "th";
	int32_t md = dayNum;
	if ( md == 1 || md == 21 || md == 31 ) suffix = "st";
	if ( md == 2 || md == 22             ) suffix = "nd";
	if ( md == 3 || md == 23             ) suffix = "rd";
	// February 12th, etc.
	if ( ! sb->safePrintf(" %s %"INT32"%s ",
			      s_mnames[month],
			      dayNum,
			      suffix))
		return false;
	return true;
}

// for "Feb. 3 - March 31 [[]] Thursdays, Feb. 3 - Mar. 31 from 6:30 - 9:30 pm"
// for denver.org, print "February 3rd to March 31st"
static bool printMonthDayRange ( SafeBuf *sb , 
				 Event *ev ,
				 Interval *int3 ,
				 Date **all ,
				 int32_t numAll ) {

	bool rangeSet = false;
	int32_t bestMonth1;
	int32_t bestMonth2;
	int32_t bestDay1;
	int32_t bestDay2;
	int32_t bestYear1;
	int32_t bestYear2;
	// . first try to print symbolically.
	// . identify all monthday ranges in teh date and do a symbolic
	//   intersection if necessary, then print that out i fpossible
	for ( int32_t i = 0 ; i < numAll ; i++ ) {
		// int16_tcut
		Date *di = all[i];
		// need daynum
		if ( di->m_type != DT_DAYNUM ) continue;
		// must be part of monthdaynum range
		Date *p1 = di->m_dateParent;
		if ( ! p1 ) continue;
		Date *p2 = p1->m_dateParent;
		if ( ! p2 ) continue;
		if ( p2->m_type != DT_RANGE_MONTHDAY ) continue;
		//
		// TODO: if it is a list of monthday ranges, forget it!
		//
		// and we must be first
		if ( p2->m_ptrs[0] != p1 ) continue;
		// record min max
		int32_t month1 = p1->m_month;
		int32_t day1   = di->m_num;
		int32_t year1  = p1->m_year;
		// get end point
		int32_t month2 = p2->m_ptrs[1]->m_month;
		int32_t day2   = p2->m_ptrs[1]->m_dayNum;
		int32_t year2  = p2->m_ptrs[1]->m_year;
		// sanity
		if ( ! rangeSet ) {
			bestMonth1 = month1;
			bestDay1   = day1;
			bestYear1  = year1;
			bestMonth2 = month2;
			bestDay2   = day2;
			bestYear2  = year2;
			rangeSet   = true;
			continue;
		}
		// intersect the ranges symbolically
		if ( year1 == bestYear1 && month1 > bestMonth1 ) {
			bestMonth1 = month1;
			bestDay1   = day1;
		}
		if ( year2 == bestYear2 && month2 < bestMonth2 ) {
			bestMonth2 = month2;
			bestDay2   = day2;
		}
	}
	// print it out symbolically?
	if ( rangeSet ) {
		if ( ! printMonthDay (sb,bestMonth1-1,bestDay1))return false;
		if ( ! sb->safePrintf(" to ") ) return false;
		if ( ! printMonthDay (sb,bestMonth2-1,bestDay2))return false;
		return true;
	}
		


	// gmtime assumes the time_t we give it is in utc time
	// which i guess it is... 
	time_t ttt1 = int3->m_a;
	// get timezone info
	char useDST;
	char tz = ev->m_address->getTimeZone(&useDST);
	// sanity
	if ( tz >= 25 ) { char *xx=NULL;*xx=0; }
	// apply that to the time
	ttt1 += 3600 * tz;
	// now we also deal with DST too!
	int32_t bonus = 0;
	if ( useDST && getIsDST(ttt1,tz) ) bonus = 3600;
	ttt1 += bonus;
	// make into time
	struct tm *ts = gmtime ( &ttt1 );

	if ( ! printMonthDay ( sb, ts->tm_mon, ts->tm_mday ) ) return false;

	if ( ! sb->safePrintf ( " to " ) ) return false;

	// find last interval
	int3 += ev->m_ni - 1;
	// endpoint
	time_t ttt2 = int3->m_b + 3600 * tz + bonus;
	// make into time
	ts = gmtime ( &ttt2 );

	if ( ! printMonthDay ( sb, ts->tm_mon, ts->tm_mday ) ) return false;

	return true;
}


// miss: Every 1st & 2nd Thursday's [[]] before 9PM [[]] 7:30PM - 11:30PM
//       from rateclubs.com... need to intersect ongoing range with other...
// really we don't deal well with multiple ranges on recurring dates...

// just print the date itself
// print normalized
bool Date::printTextNorm ( SafeBuf *sb , Words *words , bool inHtml ,
			   Event *ev , SafeBuf *intBuf ) {

	if ( ! printTextNorm2 (sb,words,inHtml,ev,intBuf) ) return false;

	if ( ! ev->m_numCloseDates ) return true;

	if ( ! sb->safePrintf(" closed ") ) return false;

	// store all closed dates in here too
	for ( int32_t i = 0 ; i < ev->m_numCloseDates ; i++ ) {
		// breathe
		//QUICKPOLL(m_niceness);
		// get it
		Date *di = ev->m_closeDates[i];
		// print it
		if ( ! di->printTextNorm2 (sb,words,false,ev,NULL) )
			return false;
	}

	return true;
}

bool Date::printTextNorm2 ( SafeBuf *sb , Words *words , bool inHtml ,
			    Event *ev , SafeBuf *intBuf ) {

	// we need this to get the timezone
	if ( ! ev->m_address ) return true;

	// point to interval buffer for all dates
	char     *bufStart = NULL;
	Interval *int3     = NULL;
	if ( intBuf ) {
		bufStart = intBuf->getBufStart();
		// int16_tcuts to the list of intervals this event has
		int3 = (Interval *)(ev->m_intervalsOff + bufStart);
	}

	// if its just one time interval print it out all pretty!
	if ( ev->m_ni == 1 && int3 ) {
		// gmtime assumes the time_t we give it is in utc time
		// which i guess it is... 
		time_t ttt1 = int3->m_a;
		// get timezone info
		char useDST;
		char tz = ev->m_address->getTimeZone(&useDST);
		// sanity
		if ( tz >= 25 ) { 
			log("date: got bad timezone of %"INT32". resetting to -6.",
			    (int32_t)tz);
			useDST = true;
			tz = -6;
		}
		// apply that to the time
		ttt1 += 3600 * tz;
		// now we also deal with DST too!
		int32_t bonus = 0;
		if ( useDST && getIsDST(ttt1,tz) ) bonus = 3600;
		ttt1 += bonus;
		// make into time
		struct tm *ts = gmtime ( &ttt1 );
		char *suffix = "th";
		int32_t md = ts->tm_mday;
		if ( md == 1 || md == 21 || md == 31 ) suffix = "st";
		if ( md == 2 || md == 22             ) suffix = "nd";
		if ( md == 3 || md == 23             ) suffix = "rd";
		// February 12th
		sb->safePrintf("%s %s %"INT32"%s %"INT32" ",
			       s_dnames[ts->tm_wday],
			       s_mnames[ts->tm_mon],
			       (int32_t)ts->tm_mday,
			       suffix,
			       (int32_t)ts->tm_year+1900);
		// endpoint
		time_t ttt2 = int3->m_b + 3600 * tz + bonus;
		// only if not same
		if ( ttt2 != ttt1 ) {
			// print each tod
			sb->safePrintf("from ");
			// first time
			printTOD ( sb , ttt1 );
			// range
			sb->safePrintf(" to ");
			// endpoint
			printTOD ( sb , ttt2 );
		}
		// else just know hte start time
		else {
			sb->safePrintf("at ");
			// first time
			printTOD ( sb , ttt1 );
		}
		return true;
	}

	// fix for "26 [[]] April 2011 [[]] ..."
	// put ALL ptrs into here
	// http://www.pridesource.com/calendar_item.html?item=9045
	// breaches the 256 limit. it has a int32_t list of compound dates
	//Date *all[1024];
	//int32_t numAll = 0;
	//::addPtrToArray ( all , &numAll , this , NULL );//&dnp );
	int32_t numAll = 0;
	Date **all = m_dates->getDateElements ( this , &numAll );
	// error?
	if ( ! all ) return false;

	Date *dowDate = NULL;
	bool hasTODRange = false;

	// . check for special format
	// . every 1st Wednesday, Tuesday through Sunday 9 a.m. to 5 p.m
	// . first Friday at 1:30pm [[]] Tues-Fri, 8-5
	// . from collectorsguide.com
	// . so if we got a compound that has a dowrange and a todrange, and
	//   the compounds brother is a recurring dow, format it nice.
	if ( m_numPtrs == 2 &&
	     ( m_ptrs[0]->m_hasType == DT_DOW ||
	       m_ptrs[0]->m_hasType == (DT_DOW|DT_TOD|DT_COMPOUND)) &&
	     // i don't care about DT_LIST_OTHER
	     (m_ptrs[1]->m_hasType | DT_LIST_OTHER)
		== (DT_RANGE_DOW|
		    DT_DOW|
		    DT_COMPOUND|
		    DT_LIST_OTHER| // multiple dow store hrs
		    DT_RANGE_TOD|
		    DT_TOD) ) {
		dowDate = m_ptrs[0];
		hasTODRange = true;
	}

	// Every Tuesday [[]] before 9PM [[]] 7:30PM - 11:30PM
	// rateclubs.com
	if ( m_numPtrs == 3 &&
	     m_ptrs[0]->m_hasType == DT_DOW &&
	     m_ptrs[1]->m_hasType == DT_TOD &&
	     m_ptrs[2]->m_hasType == (DT_TOD|DT_RANGE_TOD) ) {
		dowDate = m_ptrs[0];
		hasTODRange = true;
	}

	// reset
	bool hasMonthDayRange = false;
	// Feb. 3-March 31 [[]] Thursdays, Feb. 3 - Mar. 31 from 6:30 - 9:30pm
	// denver.org
	if ( m_numPtrs == 2 &&
	     m_ptrs[0]->m_hasType == (DT_MONTH|
				      DT_DAYNUM|
				      DT_COMPOUND|
				      DT_RANGE_MONTHDAY) &&
	     m_ptrs[1]->m_numPtrs == 3 &&
	     m_ptrs[1]->m_ptrs[0]->m_hasType == DT_DOW &&
	     m_ptrs[1]->m_ptrs[1]->m_hasType == (DT_MONTH|
						 DT_DAYNUM|
						 DT_COMPOUND|
						 DT_RANGE_MONTHDAY) &&
	     m_ptrs[1]->m_ptrs[2]->m_hasType == (DT_TOD |DT_RANGE_TOD) ) {
		dowDate = m_ptrs[1]->m_ptrs[0];
		hasTODRange = true;
		hasMonthDayRange = true;
	}

	// i've seen these first two things be true, but int3 be false
	// dunno why...
	if ( dowDate && hasTODRange && int3 ) {
		// get the recurring bits in case it is not "every"
		if ( ! printDOW ( sb , dowDate ) ) return false;
		// print like Feb 1 - Jan 28
		if ( hasMonthDayRange && 
		     ! printMonthDayRange (sb, ev, int3,all,numAll ) ) 
			return false;
		// gmtime assumes the time_t we give it is in utc time
		// which i guess it is... 
		time_t ttt1 = int3->m_a;
		// get timezone info
		char useDST;
		char tz = ev->m_address->getTimeZone(&useDST);
		// sanity
		if ( tz >= 25 ) { char *xx=NULL;*xx=0; }
		// apply that to the time
		ttt1 += 3600 * tz;
		// now we also deal with DST too!
		int32_t bonus = 0;
		if ( useDST && getIsDST(ttt1,tz) ) bonus = 3600;
		ttt1 += bonus;
		// every wednesday
		//sb->safePrintf("Every %s ",
		//	       s_dnames[m_ptrs[0]->m_num-1]);
		// endpoint
		time_t ttt2 = int3->m_b + 3600 * tz + bonus;
		// only if not same
		if ( ttt2 != ttt1 ) {
			// print each tod
			sb->safePrintf(" from ");
			// first time
			printTOD ( sb , ttt1 );
			// range
			sb->safePrintf(" to ");
			// endpoint
			printTOD ( sb , ttt2 );
		}
		// else just know hte start time
		else {
			// print each tod
			sb->safePrintf(" at ");
			// first time
			printTOD ( sb , ttt1 );
		}
		return true;
	}





	int32_t np = m_numPtrs;
	// fake it if we are it
	if ( np == 0 ) np = 1;

	// ok, add date ptrs to do not print table
	//HashTableX dnp;
	//char dnpbuf[1024];
	//dnp.set ( 4,0,32,dnpbuf,1024,false,0,"dnptbl");
	//addDoNotPrintDates ( &dnp );

	// we need to do the group # thing recursively i think
	//int32_t       groupNum = 0;
	//setGroupNumRecursive ( this , &accMask , &groupNum );
	//datetype_t accMask  = 0;

	// now move a single daynum telescope to after the month
	for ( int32_t i = 0 ; i < numAll ; i++ ) {
		if ( all[i]->m_type != DT_DAYNUM ) continue;
		if ( all[i]->m_dateParent->m_type != DT_TELESCOPE ) continue;
		// fix "7:30pm [[]] 2 [[]] SAT [[]] April 2011"
		// find next month on right
		int32_t k; for ( k = i+1 ; k < numAll ; k++ )
			if ( all[k]->m_type == DT_MONTH ) break;
		// skip if none
		if ( k >= numAll ) continue;
		// save it
		Date *tmp = all[i];
		// otherwise, put ourselves after it! shift down
		for ( int32_t j = i ; j < k ; j++ ) 
			all[j] = all[j+1];
		// go after it
		all[k] = tmp;
	}

	//
	// mark some dates as redundant for printing purposes
	//
	// . fix "after 12a.m [[]] Thursday Weekly" from printing out
	//   "after midnight every Thursday daily"
	bool recurringDow = false;
	for ( int32_t i = 0 ; i < numAll ; i++ ) {
		// get next ptr
		Date *di = all[i];
		// get parent
		Date *dp = di->m_dateParent;
		// is it a recurring dow?
		if ( di->m_type == DT_DOW &&
		     dp && 
		     (dp->m_suppFlags & SF_RECURRING_DOW) ) {
			recurringDow = true;
			continue;
		}
		// if we are "daily" or "weekly" after a recurring dow,
		// then do not print that!
		if ( di->m_type == DT_EVERY_DAY && recurringDow )
			di->m_flags |= DF_REDUNDANT;
		// turn it off now
		recurringDow = false;
	}


	//if ( m_a == 1668 && (m_type == DT_TELESCOPE) )
	//	log("hey");

	// telescope ptrs
	for ( int32_t i = 0 ; i < numAll ; i++ ) {
		// get next ptr
		Date *dp = all[i];
		// fake it if we are it...
		//if ( m_numPtrs > 0 ) dp = m_ptrs[i];
		//else                 dp = this;

		// skip if not core
		if ( dp->m_numPtrs ) continue;

		// print delim
		//if ( m_type == DT_TELESCOPE && i>0) sb->safePrintf(" [[]] ");

		// skip printing if unnecessary
		if ( dp->m_flags & DF_REDUNDANT )
			continue;

		if ( (dp->m_flags & DF_CLOSE_DATE) && inHtml )
			if ( ! sb->safePrintf("<font color=red>") )
				return false;

		//if ( dp == m_mostUniqueDatePtr && inHtml)
		//	sb->safePrintf("<u>");

		//Date *parent = NULL;
		//if ( dp != this ) parent = this;
		//Date *parent = dp->m_dateParent;

		// print each element
		if ( ! ::printDateElement ( dp , sb , words , this ) )
			return false;

		//if ( dp == m_mostUniqueDatePtr && inHtml )
		//	sb->safePrintf("</u>");

		if ( (dp->m_flags & DF_CLOSE_DATE) && inHtml )
			if ( ! sb->safePrintf("</font>") ) return false;

	}

	if ( numAll == 1024 ) {
		if ( ! sb->safePrintf("<b>... (truncated!)</b>" ) ) 
			return false;
	}

	/*
	// end in assumed year
	if ( m_flags & DF_ASSUMED_YEAR ) {
		int32_t t1 = m_minPubDate;
		// use 90 days instead of 365 since usually people will
		// indicate the year if the date is so far out
		int32_t t2 = t1 + 90*24*3600;
		if ( inHtml ) sb->safePrintf("<font color=blue>");
		sb->safePrintf(" ** %s- ",ctime(&t1));
		sb->safePrintf("%s",ctime(&t2));
		if ( inHtml ) sb->safePrintf("</font>");
	}
	*/
	return true;
}

/*
bool Date::addDoNotPrintDates ( HashTableX *dnp ) {

	datetype_t mask = 0;
	mask |= DT_YEAR;
	mask |= DT_MONTH;
	mask |= DT_LIST_MONTH;
	mask |= DT_RANGE_MONTH;
	mask |= DT_RANGE_MONTHDAY;
	mask |= DT_DAYNUM;
	mask |= DT_RANGE_DAYNUM;
	mask |= DT_LIST_DAYNUM;
	mask |= DT_TOD;
	mask |= DT_RANGE_TOD;
	mask |= DT_LIST_TOD;
	mask |= DT_DOW;
	mask |= DT_RANGE_DOW;
	mask |= DT_LIST_DOW;


	// fix "Night every Tuesday evening from 6:00pm to 9pm" for
	// nonamejustfriends.com
	//if ( m_type != DT_TELESCOPE ) return true;

	// if we have a daynum anywhere! do not print and dow info
	datetype_t badTypes = 0;
	// . if we have a single day like "Dec 5" (not ranges, then set this)
	// . not part of a list or range
	// . juvejazzgestival has:
	//   Fri and Sat 10 a.m. to 5 p.m. February 4th , 5th and 6th 2011
	if ( m_flags & DF_HAS_ISOLATED_DAYNUM ) {
		//badTypes |= DT_DOW;
		badTypes |= DT_RANGE_DOW;
		badTypes |= DT_LIST_DOW;
	}

	// fixes 9PM [[]] Sat, Apr 16 [[]] Sat, Apr 16 2011 - 9pm-3am
	// so it does not print the first 9PM
	if ( m_hasType & DT_RANGE_TOD )
		badTypes |= DT_TOD;

	// fix "Night every Tuesday evening from 6:00pm to 9pm" for
	// nonamejustfriends.com
	if ( m_hasType & DT_TOD )
		badTypes |= DT_SUBDAY;

	if ( (m_hasType & DT_DAYNUM) &&
	     !(m_hasType & DT_RANGE_DAYNUM) &&
	     !(m_hasType & DT_RANGE_MONTHDAY) ) {
		badTypes |= DT_SUBDAY;
		badTypes |= DT_SUBWEEK;
		badTypes |= DT_SUBMONTH;
		badTypes |= DT_EVERY_DAY;
		badTypes |= DT_ALL_HOLIDAYS;
	}

	datetype_t accTypes = 0;

	// scan date ptrs of di a
	for ( int32_t j = 0 ; j < m_numPtrs ; j++ ) {
		// breathe
		//QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *dj = m_ptrs[j];
		// dedup
		datetype_t rmTypes = accTypes ;
		// mask out DT_COMPOUND, etc.
		rmTypes &= mask;
		// don't mask these, they are pre-masked
		rmTypes |= badTypes;
		// do not print those
		if ( ! dj->addDoNotPrintRecursive ( rmTypes , dnp ) )
			return false;
		// do not add if was not a telescope!
		if ( m_type != DT_TELESCOPE ) continue;
		// dedup these types
		accTypes |= dj->m_hasType;
		// expand types
		if (accTypes & DT_DAYNUM) accTypes|=DT_RANGE_DAYNUM;
		if (accTypes & DT_DAYNUM) accTypes|=DT_LIST_DAYNUM;
		if (accTypes & DT_DAYNUM) accTypes|=DT_RANGE_MONTHDAY;
		if (accTypes & DT_MONTH ) accTypes|=DT_RANGE_MONTH;
		if (accTypes & DT_MONTH ) accTypes|=DT_LIST_MONTH;
	}
	return true;
}

// returns false and sets g_errno on error
bool Date::addDoNotPrintRecursive (datetype_t dt, HashTableX *dnp) {
	// if nothing, skip
	if ( dt == 0 ) return true;
	// skip for now
	//return true;
	// int16_tcut
	int32_t key = (int32_t)this;
	// ranges actually have ptrs, so check for them up top...
	if ( m_type & dt ) return dnp->addKey ( &key );
	// . stop on tod ranges...
	// . fixes 9PM [[]] Sat, Apr 16 [[]] Sat, Apr 16 2011 - 9pm-3am
	//   so it doesn't nuke its tod range!
	if ( m_type == DT_RANGE_TOD ) return true;
	// fix 
	// On the first Tues of each month, the talks are given at the 
	// Jonson Gallery. Tues-Fri 9am-4pm, Sun 1-4pm
	// normalizes to "every 1st Tuesday 9 a.m. to 4 p.m. 1 p.m. to 4 p.m." 
	// because it nuked our dows, so fix that!
	if ( m_type == DT_RANGE_DOW ) return true;
	// just to be safe, skip any range now
	if ( m_type & DT_RANGE_ANY ) return true;

	if ( m_type & DT_COMPOUND ) return true;

	for ( int32_t j = 0 ; j < m_numPtrs ; j++ ) {
		// breathe
		//QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *dj = m_ptrs[j];
		// test
		if ( ! dj->addDoNotPrintRecursive ( dt , dnp ) ) return false;
	}
	if ( m_numPtrs > 0 ) return true;
	// we are it otherwise
	if ( m_type & dt ) return dnp->addKey ( &key );
	return true;
}
*/

bool printDateElement ( Date *dp , SafeBuf *sb , Words *words ,
			Date *fullDate ) {

	// . skip if no printing!
	// . this is because it is redundant information and confuses
	//   the turks and those users viewing the event cached page
	//if ( dp->m_flags & DF_DONOTPRINT ) return true;

	// int16_tcut
	//int32_t key = (int32_t)dp;
	// skip if do not print is basically set
	//if ( dnp->isInTable ( &key ) ) return true;


	//for ( int32_t i = 0 ; i < dp->m_numPtrs ; i++ )
	//	if (!printDateElement(dp->m_ptrs[i],sb,words,dp,dnp,fullDate))
	//		return false;

	if ( dp->m_numPtrs ) return true;

	//nodeid_t *tids = words->getTagIds();
	char **wptrs = words->getWords();
	int32_t  *wlens = words->getWordLens ();

	Date *parent = dp->m_dateParent;

	// preceed with space i guess
	//if ( ! sb->pushChar(' ') ) return false;

	char *prefix = NULL;

	// sometimes we got a compound range, so check parent of parent
	// we should set m_dateParents right then, last thing we do...
	Date *parent2 = parent;
	Date *lastdp2 = dp;
	Date *dp2     = dp;
	for ( ; parent2 ; parent2 = parent2->m_dateParent ) {
		if ( parent2->m_type & DT_RANGE_ANY ) break;
		// skip if same
		//if ( parent2 == parent ) continue;
		// if ever not the first of its parent, do not print " thru "
		// . we need the first element of the 2nd in the range
		//if ( parent2->m_ptrs[0] != dp ) { lastdp2 = NULL; break; }
		if ( parent2->m_ptrs[0] != dp2 ) lastdp2 = NULL;
		// up this too
		dp2 = dp2->m_dateParent;
	}

	// same for lists!!
	Date *parent3 = parent;
	Date *lastdp3  = dp;
	Date *dp3      = dp;
	for ( ; parent3 ; parent3 = parent3->m_dateParent ) {
		if ( parent3->m_type & DT_LIST_ANY ) break;
		// if ever not the first of its parent, do not print " thru "
		// . we need the first element of the 2nd in the range
		//if ( parent3->m_ptrs[0] != dp ) { lastdp3 = NULL; break; }
		if ( parent3->m_ptrs[0] != dp3 ) lastdp3 = NULL;
		// up this too
		dp3 = dp3->m_dateParent;
	}

	// range?
	if ( parent2 &&
	     // any kind of range will do
	     (parent2->m_type & DT_RANGE_ANY) &&
	     // make sure there
	     lastdp2 &&
	     // . before the 2nd element
	     // . use m_a because ptrs will NOT match
	     lastdp2->m_a == parent2->m_ptrs[1]->m_a )
		// print through operator
		prefix = " through ";

	// tod range?
	if ( parent &&
	     // any kind of range will do
	     (parent->m_type & DT_RANGE_TOD) &&
	     // before the 2nd element
	     dp == parent->m_ptrs[1] )
		// print through operator
		prefix = " to ";

	// " at 7pm"
	if ( parent &&
	     // not the first!
	     parent->m_ptrs[0] != dp &&
	     // and single daynum not in range
	     !(parent->m_type & DT_RANGE_ANY) &&
	     // tods only for this
	     dp->m_type == DT_TOD )
		prefix = " at ";
	// " at 7pm"
	else if ( parent &&
		  // are the first!
		  parent->m_ptrs[0] == dp &&
		  // and only! (must be telescope?)
		  parent->m_numPtrs == 1 &&
		  parent->m_dateParent && // m_type == DT_TELESCOPE &&
		  parent->m_dateParent->m_ptrs[0] != dp &&
		  // not the first telescope
		  //parent->m_dateParent &&
		  //parent->m_dateParent->m_ptrs[0] != parent &&
		  // tods only for this
		  dp->m_type == DT_TOD )
		prefix = " at ";

	// open-ended ongoing ranges
	if ( dp->m_flags & DF_ONGOING ) {
		if ( dp->m_flags & DF_BEFORE_TOD )
			prefix = " until ";
		else
			prefix = " after ";
	}

	//else if ( dp->m_type == DT_TOD ) {
	//	log("hey");
	//	//char *xx=NULL;*xx=0; 
	//}

	// could be a compound that is then part of a list!
	

	// if your parent is a list print a ", " or " and "
	// before printing the date element
	if  ( parent3 && 
	      // we are in a list
	      ( parent3->m_type & DT_LIST_ANY ) && 
	      // make sure there
	      lastdp3 &&
	      // . if we are last in list...
	      // . use m_a because ptrs will NOT match
	      lastdp3->m_a == parent3->m_ptrs[parent3->m_numPtrs-1]->m_a ) {
		prefix = " and ";
	}
	else if  ( parent3 && 
		   // we are in a list
		   ( parent3->m_type & DT_LIST_ANY ) && 
		   // make sure there
		   lastdp3 &&
		   // . if we are last in list...
		   // . use m_a because ptrs will NOT match
		   lastdp3->m_a != parent3->m_ptrs[0]->m_a ) {
		prefix = ", ";
	}

	// are we first month in a list of months?
	if ( parent &&
	     parent->m_type == DT_LIST_MONTH &&
	     dp->m_type == DT_MONTH &&
	     parent->m_ptrs[0] == dp )
		prefix = " in ";
	// "every Friday 12:05pm to 1pm in January"
	// if printing a month like January and a day is not on left or
	// right of us...
	if ( dp->m_type == DT_MONTH && 
	     // not first
	     parent &&
	     parent->m_ptrs[0] != dp &&
	     // and prev brother not a month
	     parent->m_ptrs[0]->m_type != DT_MONTH &&
	     // and no daynum present
	     !(fullDate->m_hasType & DT_DAYNUM) ) 
		prefix = " in ";

	// default
	if ( ! prefix ) prefix = " ";

	if ( ! sb->safePrintf("%s", prefix) ) return false;

	// a month? make it full name
	if ( dp->m_type == DT_MONTH ) {
		int32_t mi = dp->m_month;
		if ( mi < 0 || mi >= 13 ) { char *xx=NULL;*xx=0; }
		if ( dp->m_suppFlags & SF_MID )
			if ( ! sb->safePrintf("mid-") )
				return false;
		if ( ! sb->safePrintf("%s",s_mnames[mi-1]) )
			return false;
	}
	// normalize day names
	else if ( dp->m_type == DT_DOW ) {
		if ( ! printDOW ( sb , dp ) ) return false;
	}
	// a tod?
	else if ( dp->m_type == DT_TOD ) {
		// print it out
		return printTOD ( sb , dp->m_tod );
	}		
	// a daynum?
	else if ( dp->m_type == DT_DAYNUM ) {
		int32_t dn = dp->m_dayNum;
		char *suffix = "th";
		if ( dn == 1 || dn == 21 || dn == 31 )
			suffix = "st";
		if ( dn == 2 || dn == 22 )
			suffix = "nd";
		if ( dn == 3 || dn == 23 )
			suffix = "rd";
		if ( ! sb->safePrintf("%"INT32"%s",dn,suffix) ) return false;
	}
	// year
	else if ( dp->m_type == DT_YEAR ) {
		if ( ! sb->safePrintf("%"INT32"",dp->m_year ) ) return false;
	}
	else if ( dp->m_type == DT_EVERY_DAY ) {
		if ( ! sb->safePrintf("daily" ) ) return false;
	}
	// nights mornings evening afternoon...
	else if ( dp->m_type == DT_SUBDAY ) {
		return true;
	}
	else if ( dp->m_type == DT_SUBWEEK ) {
		if ( dp->m_num == HD_TTH ) {
			if ( ! sb->safePrintf("Tuesday & Thursday" ) ) 
				return false;
		}
		else if ( dp->m_num == HD_MW ) {
			if ( ! sb->safePrintf("Monday & Wednesday" ) ) 
				return false;
		}
		else if ( dp->m_num == HD_MWF ) {
			if ( !sb->safePrintf("Monday, Wednesday, Friday"))
				return false;
		}
		else if ( dp->m_num == HD_WEEKENDS ) {
			if ( ! sb->safePrintf("Weekends")) 
				return false;
		}
		else if ( dp->m_num == HD_WEEKDAYS ) {
			if ( !sb->safePrintf("Weekdays")) 
				return false;
		}
		else { char *xx=NULL;*xx=0; }
	}
	// what is this???? summers, weekends...
	else {
		char *s  = wptrs[dp->m_a];
		int32_t len = wptrs[dp->m_b-1] - wptrs[dp->m_a] +wlens[dp->m_b-1];
		if ( ! sb->safeMemcpy( s , len ) ) return false;
	}

	// print groupnumright before then
	//if(! sb->safePrintf("<sub>%"INT32"</sub>",dp->m_groupNum) ) return false;

	return true;
}


void Date::print ( SafeBuf *sbArg , 
		   Sections *ss , 
		   Words    *words ,
		   int32_t      siteHash ,
		   int32_t num , 
		   Date *best ,
		   Dates *dates ) {

	// use this
	SafeBuf tmp;
	SafeBuf *sb = sbArg;
	if ( ! sbArg ) sb = &tmp;

	// bold strings
	char *b1 = "";
	char *b2 = "";
	if ( this == best ) { b1 = "<b>"; b2 = "</b>"; }

	char *f1 = "";
	char *f2 = "";
	// were we part of a compound date or whatever?
	if ( ! ( m_flags & DF_NOTKILLED ) ) {
		f1 = "<font color=gray><strike>"; f2 = "</strike></font>"; }

	// show it
	if ( sbArg ) 
		sb->safePrintf("<tr>\n"
			       // tell diff to ignore
			       "<!--ignore--><td>%s%s#%"INT32"%s%s</td>\n" 
			       "<td>%"INT32"</td>"
			       "<td>%"INT32"</td>"    ,
			       f1,b1,num,b2,f2,
			       m_a,m_b );
	else
		sb->safePrintf("%s#%"INT32"%s | " 
			       "%"INT32" | "
			       "%"INT32" | "    ,
			       b1,num,b2,
			       m_a,m_b );
		

	// show the text of the date
	if ( sbArg ) sb->safePrintf("<td><nobr>");
	if ( m_flags & DF_INVALID ) sb->safePrintf("<strike>");
	sb->safePrintf("%s",f1);
	if ( m_a >= 0 && m_b >= 0 ) {
		printText ( sb , words );
	}
	else
		sb->safePrintf("???");
	sb->safePrintf("%s",f2);
	if ( m_flags & DF_INVALID ) sb->safePrintf("</strike>");

	// end in assumed year
	//if ( m_flags & DF_ASSUMED_YEAR )
	//	sb->safePrintf(" ** %"INT32"",m_year);


	if ( sbArg ) sb->safePrintf("</nobr></td>");
	else         sb->safePrintf(" | ");
	
	// timestamp

	struct tm *timeStruct = localtime ( &m_timestamp );
	// assume numeric timestamps are already in UTC?
	if ( m_type == DT_TIMESTAMP )
		timeStruct = gmtime ( &m_timestamp );
	char time[256];
        strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	if ( m_timestamp == 0 ) strcpy(time,"---");

	//TimeZone *tzPtr = m_pubDateTimeZones[i];
	//char *tzStr = "&nbsp;";
	//if ( tzPtr ) tzStr = tzPtr->m_name;

	// some other junk
	if ( sbArg ) {
		sb->safePrintf("<td><nobr>%s%"INT32"%s</nobr></td>"      // score
			       "<td><nobr>%s</nobr></td>" // timetamp
			       "<td>%s</td>",
			       b1,-1 * m_penalty,b2,
			       time,
			       "---");//tzStr);
	}
	else
		sb->safePrintf("%s%"INT32"%s | "      // score
			       "%s | " // timetamp
			       // timezone
			       "%s"
			       ,
			       b1,-1 * m_penalty,b2,
			       time,
			       "---");//tzStr);

	// sentence id
	//sb->safePrintf("<td>%"INT32"</td>",m_sentenceId);

	// datehash64
	sb->safePrintf("<td>%"UINT64"</td>",m_dateHash64);
	// . tag hash
	// . turkTagHash is for date elements really
	sb->safePrintf("<td>%"UINT32"</td>",m_dateTypeAndTagHash32);//m_turkTagHash);
	
	// flag row
	sb->safePrintf("<td><nobr>");

	// print each flag

	if ( m_headerCount )
		sb->safePrintf("(hdrcnt=%"INT32") ",m_headerCount);
	if ( m_tmph )
		sb->safePrintf("(deduphash=0x%"XINT32") ",m_tmph);
	if ( m_maxYearGuess )
		sb->safePrintf("(maxyearguess=%"INT32") ",m_maxYearGuess);
	if ( m_dowBasedYear )
		sb->safePrintf("(dowbasedyear=%"INT32") ",m_dowBasedYear);

	if ( m_flags & DF_DUP ) {
		int32_t dupNum = dates->getDateNum(m_dupOf);
		sb->safePrintf("dupof%"INT32" ",dupNum);
	}
	if ( m_flags & DF_SUB_DATE ) {
		int32_t dnum = dates->getDateNum(m_subdateOf);
		sb->safePrintf("subdateof%"INT32" ",dnum);
	}

	if ( m_flags & DF_EVENT_CANDIDATE )
		sb->safePrintf("<b>eventcandidate</b> ");

	if ( m_flags & DF_ASSUMED_YEAR )
		sb->safePrintf("assumedyear ");
	if ( m_flags & DF_YEAR_UNKNOWN )
		sb->safePrintf("<b><font color=blue>yearunkown</font></b> ");

	if ( m_flags & DF_STORE_HOURS )
		sb->safePrintf("<font color=green><b>storehours</b></font> ");
	if ( m_flags & DF_SUBSTORE_HOURS )
		sb->safePrintf("<font color=green><b>substorehours</b></font> ");

	if ( m_flags & DF_WEEKLY_SCHEDULE )
		sb->safePrintf("<font color=green><b>weeklysched</b></font> ");
	if ( m_flags & DF_KITCHEN_HOURS )
		sb->safePrintf("kitchenhours ");
	if ( m_flags & DF_SCHEDULECAND )
		sb->safePrintf("schedulecand ");

	if ( m_flags & DF_TIGHT )
		sb->safePrintf("tight ");
	if ( m_flags & DF_INCRAZYTABLE )
		sb->safePrintf("incrazytable ");
	if ( m_flags & DF_TABLEDATEHEADERROW )
		sb->safePrintf("tabledateheaderrow ");
	if ( m_flags & DF_TABLEDATEHEADERCOL )
		sb->safePrintf("tabledateheadercol ");

	if ( m_flags & DF_IN_LIST )
		sb->safePrintf("indatelist ");
	if ( m_flags & DF_FIRST_IN_LIST )
		sb->safePrintf("firstindatelist ");

	if ( m_flags & DF_REDUNDANT )
		sb->safePrintf("redundant ");
	if ( m_flags & DF_HAS_ISOLATED_DAYNUM )
		sb->safePrintf("hasisolateddaynum ");
	if ( m_flags & DF_IN_CALENDAR )
		sb->safePrintf("incalendar ");
	if ( m_calendarSection )
		sb->safePrintf("incalendarsection ");
	if ( m_flags & DF_REGISTRATION )
		sb->safePrintf("<b>registration</b> ");
	if ( m_flags & DF_NONEVENT_DATE )
		sb->safePrintf("<b>noneventdate</b> ");
	if ( m_flags5 & DF5_IGNORE )
		sb->safePrintf("<b><font color=red>ignore</font></b> ");

	if ( m_flags & DF_ONOTHERPAGE )
		sb->safePrintf("<font color=blue><b>onotherpage</b></font> ");

	//if ( m_flags & DF_UPSIDEDOWN )
	//	sb->safePrintf("upsidedown ");
	//if ( m_flags & DF_GOOD_EVENT_DATE )
	//	sb->safePrintf("goodeventdate ");

	if ( m_flags & DF_HAS_WEAK_DOW )
		sb->safePrintf("hasweakdow ");
	if ( m_flags & DF_HAS_STRONG_DOW )
		sb->safePrintf("hasstrongdow ");

	//if ( m_flags & DF_IN_VERTICAL_LIST )
	//	sb->safePrintf("inverticallist ");
	if ( m_flags & DF_INVALID )
		sb->safePrintf("invalid ");
	//if ( m_flags & DF_BAD_ENDING )
	//	sb->safePrintf("badending ");
	//if ( m_flags & DF_IN_SAME_SENTENCE )
	//	sb->safePrintf("insamesentence ");
	if ( m_flags & DF_CLOSE_DATE ) 
		sb->safePrintf("<font color=red><b>closed</b></font> ");
	//if ( m_flags & DF_BAD_RECURRING_DOW )
	//	sb->safePrintf("badrecurringdow ");

	if ( m_flags & DF_PUB_DATE )
		sb->safePrintf("pubdate ");
	//if ( m_flags & DF_COMMENT_DATE )
	//	sb->safePrintf("commentdate ");
	// not allowed to be recurring
	//if ( m_flags & DF_FUNERAL_DATE )
	//	sb->safePrintf("funeraldate ");

	if ( m_suppFlags & SF_RECURRING_DOW )
		sb->safePrintf("recurringdow ");
	//if ( m_suppFlags & SF_DOW_IN_TITLE )
	//	sb->safePrintf("dowintitle ");
	if ( m_suppFlags & SF_PLURAL )
		sb->safePrintf("plural ");
	if ( m_suppFlags & SF_NON )
		sb->safePrintf("non ");
	if ( m_suppFlags & SF_MID )
		sb->safePrintf("mid ");
	if ( m_suppFlags & SF_EVERY )
		sb->safePrintf("every ");
	if ( m_suppFlags & SF_FIRST )
		sb->safePrintf("first ");
	if ( m_suppFlags & SF_LAST )
		sb->safePrintf("last ");
	if ( m_suppFlags & SF_SECOND )
		sb->safePrintf("second ");
	if ( m_suppFlags & SF_THIRD )
		sb->safePrintf("third ");
	if ( m_suppFlags & SF_FOURTH )
		sb->safePrintf("fourth ");
	if ( m_suppFlags & SF_FIFTH )
		sb->safePrintf("fifth ");
	if ( m_suppFlags & SF_HAD_AMPM )
		sb->safePrintf("hadampm ");
	if ( m_suppFlags & SF_PM_BY_LIST )
		sb->safePrintf("pmbylist ");
	if ( m_suppFlags & SF_MILITARY_TIME )
		sb->safePrintf("militarytime ");
	if ( m_suppFlags & SF_IMPLIED_AMPM )
		sb->safePrintf("impliedampm ");
	if ( m_suppFlags & SF_NIGHT )
		sb->safePrintf("night ");
	if ( m_suppFlags & SF_AFTERNOON )
		sb->safePrintf("afternoon ");
	if ( m_suppFlags & SF_MORNING )
		sb->safePrintf("morning ");
	if ( m_suppFlags & SF_ON_PRECEEDS )
		sb->safePrintf("onpreceeds ");
	if ( m_suppFlags & SF_SPECIAL_TOD )
		sb->safePrintf("specialtod ");

	//if ( m_suppFlags & SF_NON_FUZZY )
	//	sb->safePrintf(" ");

	if ( m_flags & DF_COPYRIGHT )
		sb->safePrintf("copyright ");
	if ( m_flags & DF_IN_HYPERLINK )
		sb->safePrintf("inhyperlink ");
	if ( m_flags & DF_ONGOING )
		sb->safePrintf("ongoing ");
	if ( m_flags & DF_AFTER_TOD )
		sb->safePrintf("aftertod ");
	if ( m_flags & DF_BEFORE_TOD )
		sb->safePrintf("beforetod ");
	if ( m_flags & DF_EXACT_TOD )
		sb->safePrintf("exacttod ");
	if ( m_tableCell ) {
	       sb->safePrintf("tablesec=0x%"PTRFMT" ",
			      (PTRTYPE)m_tableCell->m_tableSec);
		sb->safePrintf("row=%"INT32" ",m_tableCell->m_rowNum);
		sb->safePrintf("col=%"INT32" ",m_tableCell->m_colNum);
	}

	if ( m_flags & DF_LEFT_BOOKEND )
		sb->safePrintf("leftbookend ");
	if ( m_flags & DF_RIGHT_BOOKEND )
		sb->safePrintf("rightbookend ");
	if ( m_flags & DF_HARD_LEFT )
		sb->safePrintf("hardleft ");
	if ( m_flags & DF_HARD_RIGHT )
		sb->safePrintf("hardright ");
	if ( m_flags & DF_FUZZY )
		sb->safePrintf("fuzzy ");
	if ( m_flags & DF_USEDASHEADER )
		sb->safePrintf("usedasheader ");
	//if ( m_flags & DF_GOOD_EVENT_DATE )
	//	sb->safePrintf("goodeventdate ");

	//if ( m_flags & DF_NEEDMOREINFO )
	//	sb->safePrintf("<font color=red><b>needmoreinfo"
	//		       "</b></font> ");
	if ( m_flags & DF_OFFICIAL )
		sb->safePrintf("<b>officialtime</b> ");
	if ( m_flags & DF_CLOCK )
		sb->safePrintf("<b>clock</b> ");
	if ( m_flags & DF_NOTCLOCK )
		sb->safePrintf("<b>notclock</b> ");


	// . get the flags from section voting from datedb list
	// . Section::m_voteFlags are set in Sections.cpp
	Section *sn = NULL;
	if ( m_flags & DF_FROM_BODY ) 
		// use m_a not m_b...?
		sn = ss->m_sectionPtrs[m_a];

	// should be -1 if no voters!
	//float v1 = dates->m_osvt->getScore      ( sn , SV_CLOCK );
	//float n1 = dates->m_osvt->getNumSampled ( sn , SV_CLOCK );
	//if ( n1 > 0 ) 
	//	sb->safePrintf("<b>sec_clock(%.02f[%f])</b> ",v1,n1);

	if ( m_hasType & DT_TOD )
		sb->safePrintf("TOD ");
	if ( m_hasType & DT_DAYNUM )
		sb->safePrintf("DAYNUM ");
	if ( m_hasType & DT_MONTH )
		sb->safePrintf("MONTH ");
	if ( m_hasType & DT_YEAR )
		sb->safePrintf("YEAR ");
	if ( m_hasType & DT_DOW ) // day of week
		sb->safePrintf("DOW ");
	//if ( m_hasType & DT_MOD ) // "first" "last" "second"
	//	sb->safePrintf("MOD ");
	if ( m_hasType & DT_HOLIDAY )
		sb->safePrintf("HOLIDAY ");
	if ( m_hasType & DT_SUBDAY )
		sb->safePrintf("SUBDAY ");
	if ( m_hasType & DT_SUBWEEK )
		sb->safePrintf("SUBWEEK ");
	if ( m_hasType & DT_SUBMONTH )
		sb->safePrintf("SUBMONTH ");
	if ( m_hasType & DT_EVERY_DAY )
		sb->safePrintf("EVERYDAY ");
	if ( m_hasType & DT_SEASON )
		sb->safePrintf("SEASON ");
	if ( m_hasType & DT_ALL_HOLIDAYS )
		sb->safePrintf("ALLHOLIDAYS ");
	if ( m_hasType & DT_TIMESTAMP )
		sb->safePrintf("TIMESTAMP ");
	if ( m_hasType & DT_RANGE )
		sb->safePrintf("RANGE ");
	if ( m_hasType & DT_RANGE_YEAR )
		sb->safePrintf("RANGEYEAR ");
	if ( m_hasType & DT_RANGE_TOD )
		sb->safePrintf("RANGETOD ");
	if ( m_hasType & DT_RANGE_DOW )
		sb->safePrintf("RANGEDOW ");
	if ( m_hasType & DT_RANGE_TIMEPOINT )
		sb->safePrintf("RANGETIMEPOINT ");
	if ( m_hasType & DT_RANGE_DAYNUM )
		sb->safePrintf("RANGEDAYNUM ");
	if ( m_hasType & DT_RANGE_MONTHDAY )
		sb->safePrintf("RANGEMONTHDAY ");
	if ( m_hasType & DT_LIST_DAYNUM )
		sb->safePrintf("LISTDAYNUM ");
	if ( m_hasType & DT_LIST_TOD )
		sb->safePrintf("LISTTOD ");
	if ( m_hasType & DT_LIST_DOW )
		sb->safePrintf("LISTDOW ");
	if ( m_hasType & DT_LIST_MONTH )
		sb->safePrintf("LISTMONTH ");
	if ( m_hasType & DT_LIST_MONTHDAY )
		sb->safePrintf("LISTMONTHDAY ");
	if ( m_hasType & DT_LIST_OTHER )
		sb->safePrintf("LISTOTHER ");
	if ( m_hasType & DT_COMPOUND )
		sb->safePrintf("COMPOUND ");
	if ( m_hasType & DT_TELESCOPE )
		sb->safePrintf("TELESCOPE ");
	
	if ( m_flags & DF_FUTURE )
		sb->safePrintf("<b>infuture</b> ");
	if ( m_flags & DF_ESTIMATED )
		sb->safePrintf("estimated ");
	//if ( m_flags & DF_INHYPERLINK )
	//	sb->safePrintf("inhyperlink ");
	if ( m_flags & DF_FROM_BODY )
		sb->safePrintf("frombody ");
	if ( m_flags & DF_FROM_URL )
		sb->safePrintf("fromurl ");
	//if ( m_flags & DF_FROM_RSS )
	//	sb->safePrintf("fromrss ");
	if ( m_flags & DF_FROM_RSSINLINK )
		sb->safePrintf("rssinlink ");
	if ( m_flags & DF_FROM_RSSINLINKLOCAL )
		sb->safePrintf("rssinlinklocal ");
	if ( m_flags & DF_FROM_META )
		sb->safePrintf("frommeta ");

	if ( m_flags & DF_UNIQUETAGHASH )
		sb->safePrintf("uniquetaghash ");
	else
		sb->safePrintf("repeatedtaghash ");

	if ( m_flags & DF_AMBIGUOUS )
		sb->safePrintf("ambiguous ");
	if ( m_flags & DF_AMERICAN )
		sb->safePrintf("american ");
	if ( m_flags & DF_EUROPEAN )
		sb->safePrintf("european ");
	
	//float v2 = dates->m_osvt->getScore      ( sn , SV_EURDATEFMT );
	//float n2 = dates->m_osvt->getNumSampled ( sn , SV_EURDATEFMT );
	//if ( n2 > 0 )
	//	sb->safePrintf("sec_european(%.02f[%f]) ",v2,n2);
	
	if ( m_flags & DF_MONTH_NUMERIC )
		sb->safePrintf("monthnumeric ");
	// this means we did not find something like "1:33 pm"
	// near the date!
	if ( m_flags & DF_NOTIMEOFDAY )
		sb->safePrintf("notimeofday ");
	if ( m_flags & DF_MATCHESURLDAY )
		sb->safePrintf("matchesurlday ");
	if ( m_flags & DF_MATCHESURLMONTH )
		sb->safePrintf("matchesurlmonth ");
	if ( m_flags & DF_MATCHESURLYEAR )
		sb->safePrintf("matchesurlyear ");
	if ( m_flags & DF_INBADTAG )
		sb->safePrintf("inbadtag ");
	if ( m_flags & DF_BEFORE1970 )
		sb->safePrintf("before1970 ");
	//if ( m_flags & DF_HAS_YEAR )
	//	sb->safePrintf("hasyear ");
	if ( m_flags & DF_CANONICAL )
		sb->safePrintf("canonical ");

	if ( m_dayNum >= 0 )
		sb->safePrintf("daynum=%"INT32" ",(int32_t)m_dayNum);
	if ( m_minDayNum < 32 )
		sb->safePrintf("mindaynum=%"INT32" ",(int32_t)m_minDayNum);
	if ( m_maxDayNum > 0 )
		sb->safePrintf("maxdaynum=%"INT32" ",(int32_t)m_maxDayNum);
	if ( m_month >= 0 )
		sb->safePrintf("month=%"INT32" ",(int32_t)m_month);
	if ( m_tod >= 0 )
		sb->safePrintf("tod=%"INT32" ",(int32_t)m_tod);
	if ( m_minTod < 30*3600 )
		sb->safePrintf("mintod=%"INT32" ",(int32_t)m_minTod);
	if ( m_maxTod > 0 )
		sb->safePrintf("maxtod=%"INT32" ",(int32_t)m_maxTod);
	if ( m_dowBits )
		sb->safePrintf("dowbits=0x%"XINT32"[%"INT32"] ",
			       (uint32_t)((unsigned char)m_dowBits),
			       getNumBitsOn8(m_dowBits));
	if ( m_minYear != 2050 )
		sb->safePrintf("minyear=%"INT32" ",(int32_t)m_minYear);
	if ( m_maxYear != 1900 )
		sb->safePrintf("maxyear=%"INT32" ",(int32_t)m_maxYear);
	

	sb->safePrintf("datehash=0x%"XINT64" ",m_dateHash64);


	// make this
	int64_t termId = hash64 ( m_clockHash , siteHash );
	// mask it
	termId &= TERMID_MASK;

	if ( sbArg )
		sb->safePrintf("</nobr></td>"
			       //"<td>%"INT32"</td>"      // wordNum
			       "<td>0x%08"XINT32"</td>"  // tagHash
			       "<td>%"UINT32"</td>"      // occNum
			       "<td>0x%08"XINT32"</td>"  // clockhash
			       "<td>%"UINT64"</td>" // termid
			       "</tr>\n"           ,
			       m_tagHash    ,
			       m_occNum     ,
			       m_clockHash  ,
			       termId       );
	else
		sb->safePrintf("0x%08"XINT32" | "  // tagHash
			       "%"UINT32" | "      // occNum
			       "0x%08"XINT32" | "  // clockhash
			       "%"UINT64"" // termid
			       "\n"           ,
			       m_tagHash    ,
			       m_occNum     ,
			       m_clockHash  ,
			       termId       );


	if ( ! sbArg )
		fprintf(stdout,"%s",sb->getBufStart() );
}

// returns false if no dates
bool Dates::getDateOffsets ( Date *date ,
			     int32_t  num ,
			     int32_t *dateStartOff ,
			     int32_t *dateEndOff   ,
			     int32_t *dateSentStartOff ,
			     int32_t *dateSentEndOff ) {
	
	//nodeid_t *tids = words->getTagIds();
	char **wptrs = m_words->getWords();
	int32_t  *wlens = m_words->getWordLens ();

	//if ( m_numPtrs == 0 && (m_flags & DF_CLOSE_DATE) )
	//if ( (m_flags & DF_CLOSE_DATE) )
	//	sb->safePrintf("<font color=red>");

	// assume none
	*dateStartOff     = -1;
	*dateEndOff       = -1;
	//*dateSentStartOff = -1;
	//*dateSentEndOff   = -1;

	char *docStart = m_words->m_words[0];

	// assume this date
	Date *dp = date;

	// if not telescope...d one
	if ( num > 0 ) {
		if ( date->m_type != DT_TELESCOPE ) return false;
		if ( num >= date->m_numPtrs       ) return false;
		// get next ptr
		dp = date->m_ptrs[num];
	}

	char *p    = wptrs[dp->m_a];
	char *pend = wptrs[dp->m_b-1] + wlens[dp->m_b-1];

	*dateStartOff = p    - docStart;
	*dateEndOff   = pend - docStart;

	return true;
	/*
	// . get sentence offsets
	// . get section of the date
	Section *sd = dp->m_section;
	// scan up until sentence section
	for ( ; sd ; sd = sd->m_parent ) 
		if ( sd->m_flags & SEC_SENTENCE ) break;

	char *s    = wptrs[sd->m_a];
	char *send = wptrs[sd->m_b-1] + wlens[sd->m_b-1];
	*dateSentStartOff = s    - docStart;
	*dateSentEndOff   = send - docStart;

	return true;
	*/
}


// . returns  start time if we had a legit one
// . returns -1 and sets g_errno on error
// . returns -2 if no time found

// . "Shows at 4, 6 and 8pm on Friday and 1pm on Saturday with an "
//   "introduction by Traga Rinpoche before the 6pm Friday show."
// . - http://www.dailylobo.com/index.php/calendar/event/9m6q6esn96icvmme44iu3h79cc?time=1252706400

// . also for movie times like " 11:45am, 2:20, 4:55, 7:30, 7:55, 10:05, 10:30"
//   assume that all times with a colon are pm unless otherwise stated.
int32_t Dates::parseTimeOfDay3 ( Words     *w             ,
			      int32_t       i             ,
			      int32_t       niceness      ,
			      int32_t      *endWordNum    ,
			      TimeZone **tzPtr         ,
			      bool       monthPreceeds ,
			      // do we KNOW if it was am or pm?
			      bool      *hadAMPM       ,
			      bool      *hadMinute     ,
			      bool      *isMilitary    ) {

	int32_t           nw = w->getNumWords();
	char      **wptrs = w->getWords   ();
	int32_t       *wlens = w->getWordLens();
	nodeid_t    *tids = w->m_tagIds;
	int64_t   *wids = w->getWordIds();

	// save it
	int32_t savei = i;

	// must start with a number
	//if ( ! is_digit(wptrs[i][0]) ) { 
	//	if ( m_wids[i] != h_noon && 
	//	     m_wids[i] != h_midnight ) { char *xx=NULL;*xx=0; }
	//}
	// if length is two both must be digit ("9p"?)
	if ( wlens[i]>=2 && 
	     ! is_digit(wptrs[i][1]) &&
	     to_lower_a(wptrs[i][1]) != 'p' &&
	     to_lower_a(wptrs[i][1]) != 'a' )
		return -2;
	// 5+ is bad ("12pm" is like the biggest?)
	if ( wlens[i] >= 5 ) return -2;
	// get as number
	int32_t hour = w->getAsLong(i);
	// . must be valid hour
	// . allow for 00:29:00 GMT like for trumba.com!
	if ( hour < 0 || hour > 24 ) return -2;
	// are we military time?
	if ( hour > 12 ) *isMilitary = true;
	else             *isMilitary = false;

	// starting with 0 is military. oh-six-hundred.
	if ( wptrs[i][0] == '0' ) *isMilitary = true;

	// this will need to be true
	bool gotIt = false;
	// assume no minute follows
	bool hadMin = false;
	bool hadPeriod = false;
	// a period after the number?
	int32_t numDigits = 0;
	if ( numDigits == 0 && is_digit(wptrs[i][0]) ) numDigits++;
	if ( numDigits == 1 && is_digit(wptrs[i][1]) ) numDigits++;
	if ( numDigits == 2 && is_digit(wptrs[i][2]) ) numDigits++;
	if ( numDigits >= 3 ) return -2;
	// if a minute follows, must be like 04:32
	int32_t minute = 0;
	// support ceder.net's "6:30 - 8.00PM" !!!! allow periods
	if ( i+2<nw && (wptrs[i+1][0] == ':' || wptrs[i][numDigits]=='.')) {
		minute = w->getAsLong(i+2);
		if ( minute < 0 || minute > 59 ) return -2;
		// must be at least two chars
		if ( wlens[i+2] < 2 ) return -2;
		// flag this
		if ( wptrs[i][numDigits]=='.' ) 
			hadPeriod = true;
		// point to the minute
		i += 2;
		// flag it
		hadMin = true;
		// this too
		*hadMinute = true;
	}
	// does a second follow? "19:35:12 GMT"
	int32_t sec = 0;
	if ( i+2<nw && wptrs[i+1][0] == ':' ) {
		sec = w->getAsLong(i+2);
		if ( sec < 0 || sec > 59 ) return -2;
		// point to the second
		i += 2;
	}
	// timezone unknown at this point
	if ( tzPtr ) *tzPtr = NULL;
	// . assume end of it. point to word after that last number
	// . we kinda have to stop here because our quest for finding an "am"
	//   or "pm" often takes us over another time, because we are part of
	//   a time range like "9 - 11am".
	*endWordNum = i + 1;

	// is it pm? am?
	bool isPM = false;
	bool isAM = false;
	bool isMil = false;

	// int16_tcuts
	//int64_t h_and      = hash64b("and");
	//int64_t h_to       = hash64b("to");
	//int64_t h_noon     = hash64b("noon");
	//int64_t h_midnight = hash64b("midnight");

	// does "noon" follow the number, which must be 12
	if ( i + 2 < nw && hour == 12 && minute==0 && wids[i+2]==h_noon ) {
		// it is legit
		gotIt = true;
		// assume am
		//isAM = true;
		isPM = true;
		// update the end of it
		*endWordNum = i + 3;
	}

	if ( i + 2 < nw && hour == 12 && minute==0 && wids[i+2]==h_midnight){
		// it is legit
		gotIt = true;
		// assume pm
		isAM = false;
		// update the end of it
		*endWordNum = i + 3;
		// make hour 24
		hour = 24;
	}

	// limit am/pm scan to 10 words
	int32_t kmax = i + 10;
	if ( kmax > nw ) kmax = nw;
	char *s;
	// falg
	bool hadCrap = false;
	bool hadRangeIndicator = false;
	bool hadTODAfter = false;
	bool lastPunctWordHadJunk = false;
	bool hadTag = false;

	// count words after the numeric time stuff. looking for am or a. m.
	// etc. kinda cruft
	//int32_t additional = 0;
	//int32_t hadPunct   = 0;
	int32_t scannedHours =  0;
	int32_t followingNum = -1;
	// flag init
	//int32_t end = -1;
	// start with current word, it might have "pm" in it as substr
	for ( int32_t k = savei ; k < kmax ; k++ ) { 
		// breathe
		QUICKPOLL(niceness);
		// stop if a tag. no some crappy pages have tags between
		// the "1" and the "pm" !! maybe a <br> tag??? could be.
		if ( tids[k] ) {
			// treat a tag as a whitespace for our purposes
			lastPunctWordHadJunk = false;
			hadTag = true;
			continue;//break;
		}
		// skip if not alnum word
		if ( ! wids[k] ) {
			// mark if we had a tod after
			if ( wptrs[k][0]=='-' && 
			     followingNum == -1 )
				hadRangeIndicator = true;
			// if we had no colon (minute) after us but the 
			// following hour does, then reject us as a tod.
			// fixes "Route 8 9:00 pm - 1:00 am" for
			// http://www.guysndollsllc.com/page5/page4/page4.html
			if ( wptrs[k][0]==':' &&
			     followingNum >= 1 &&
			     ! hadMin &&
			     ! hadRangeIndicator &&
			     // we must be preceeded by a space then another
			     // word... i.e. we are in a closer grouping to
			     // this other word than we are the following
			     // time of day...
			     i-2 >= 0 &&
			     w->isSpaces(i-1) &&
			     wids[i-2] )
				return -2;
			if ( wptrs[k][0]==':' && 
			     followingNum >= 0 &&
			     hadRangeIndicator )
				hadTODAfter = true;
			// . 1) a beautiful day
			// . set lastPunctWordHadJunk for use below
			char *p    =     wptrs[k];
			char *pend = p + wlens[k];
			lastPunctWordHadJunk = false;
			for ( ; p < pend ; p++ ) {
				QUICKPOLL(niceness);
				if ( is_wspace_a(*p) ) continue;
				lastPunctWordHadJunk = true;
				break;
			}
			continue;
		}
		// skip if "and" (example: "1, 4 and 6pm")
		if ( wids[k] == h_and ) {
		     //wids[k] == h_to ||
		     //wids[k] == h_through ) { 
			// set this to indicate what we print out. i.e.
			// what string the time itself is
			hadCrap = true;
			continue;
		}
		if ( wids[k] == h_to ||
		     wids[k] == h_through ||
		     wids[k] == h_though  || // misspelling
		     wids[k] == h_until ||
		     wids[k] == h_til     ||
		     wids[k] == h_till    ||
		     wids[k] == h_thru ) { 
			hadCrap = true;
			hadRangeIndicator = true;
			continue;
		}
		// did we have midnight?
		// if we had something like 8:30 to midnight, assume we
		// are pm then!
		if ( wids[k] == h_midnight && ! isAM ) 
			isPM = true;
		if ( wids[k] == h_midday && ! isAM )
			isPM = true;
		if ( wids[k] == h_dusk && ! isAM )
			isPM = true;
		if ( wids[k] == h_sunset && ! isAM )
			isPM = true;
		if ( wids[k] == h_sundown && ! isAM )
			isPM = true;
		if ( wids[k] == h_dawn && ! isPM )
			isAM = true;
		if ( wids[k] == h_sunrise && ! isPM )
			isAM = true;
		// point to it
		s = wptrs[k];
		// and the end of it
		char *send = wptrs[k] + wlens[k];
		// if this is a number, count it
		if ( is_digit(*s) && k >= *endWordNum ) {
			// set this
			hadCrap = true;
			// get the number
			int32_t num = w->getAsLong(k);
			// stop if a year or something
			if ( num < 0 || num >= 60 ) break;
			// count it
			scannedHours++;
			// record it if first one
			if ( followingNum == -1 ) followingNum = num;
		}
		// skip over all digits
		for ( ; s < send && is_digit(*s) ; s++ ) ;
		// if they were all digits, try try next word.
		if ( s >= send ) continue;
		// get first alpha char as lower case
		char c = to_lower_a(*s);
		// if first letter is not a or p, forget it (h is military hrs)
		if ( c != 'a' && c != 'p' && c != 'h' ) break;
		// . is the 'a' or 'p' part of the hour/minute (in same word?)
		// . "1am" or "2p" or "3:30p" etc.
		bool AMPMConnected = false;
		if ( is_digit(wptrs[k][0]) ) AMPMConnected = true;
		// . watch out for sentences starting with "A"
		// . but do allow "1A "???
		if ( *s == 'A' && is_wspace_utf8 (s+1) ) break;
		// fix "3</div>3:30pm" for terrence wilson because it
		// was thinking the "3" which was the monthday (April 3rd)
		// was a tod! so don't cross tags AND another number when 
		// looking for the am pm 
		if ( followingNum >= 0 && hadTag )
			break;
		// skip the 'a' or 'p'
		s++;
		// use "t" to search for a following 'm'
		char *t = s;
		// skip period, if any
		if ( *t == '.' ) t++;
		// skip a space if any
		if ( *t == ' ' ) t++;
		// and another even
		if ( *t == ' ' ) t++;
		// check for the 'm' after the 'a' or 'p'
		if ( to_lower_a(*t) != 'm' ) {
			// . might have been "7:30p". 
			// . forget it if we had "7px" or something
			if ( is_alnum_utf8(s) ) break;
			// if a punct was between the tod and the 'a' or 'p'
			// then do not allow it through! "1) a beautiful"
			// for newyork.sa-people.com
			if ( ! AMPMConnected && lastPunctWordHadJunk ) break;
			// ok we got "1) a beautiful" or whatever..
			// maybe require 't' to be non alpha unless
			// its a month or something
			//if ( ! is_alpha_utf8(t) ) goto skip;
			// is it a month name?
			//if ( k+2<nw && getMonth(wids[k+2])>= 0 ) goto skip;
			// all shucks, ignore the "a"
			//break;
			goto skip;
		}
		// skip the m
		t++;
		// must not be another alnum after that
		if ( is_alnum_utf8 (t) ) break;
		// update s to t to include the "m"
		s = t;
		// ok, we got it
	skip:
		gotIt = true;
		// set the flag
		if      ( c == 'p' ) isPM  = true;
		else if ( c == 'a' ) isAM  = true;
		else if ( c == 'h' ) isMil = true; // military time. 7:00 h
		// do not update endWordNum if we had alnum words that
		// were not "am" or "pm" per se 
		if ( hadCrap ) break;
		// reset this
		int32_t qq;
		// now identify the last word in our time because
		// we must identify a range of words in the time.
		for ( qq = *endWordNum ; qq < nw ; qq++ )
			// if this word is passed our last char, stop
			if ( wptrs[qq] >= s ) break;
		// ok, we got it!
		*endWordNum = qq;
		// all done
		break;
	}

	// . fix "11-1pm" for panjea.org url
	// . and fix "11:30-1:30pm" ?
	// . watch out for "12:30-2pm" though!
	if ( scannedHours >= 1 && followingNum < hour && isPM && hour < 12 ) {
		// swap these
		isAM = true; 
		isPM = false;
	}
	// fix for "11:00-12:00 PM"
	if ( scannedHours >= 1 && followingNum == 12 && isPM && hour < 12 ) {
		// swap these
		isAM = true; 
		isPM = false;
	}

	// a hack fix for "Daily 9-5:30"
	if ( hadTODAfter ) {
		hadMin = true;
		*hadMinute = true;
	}

	// if had to skip a couple of words looking for am/pm and a month
	// preceeded and no ":"... but if we had an am/pm then let it through!
	// otherwise "April 10 11-1pm" fails to recognize "11" as a time of
	// day since it has monthPreceeds set to true..
	if ( monthPreceeds && ! hadMin && ! isAM && ! isPM )
		return -2;

	// or if we had to scan more than 1 additional hour to find the
	// am/pm we are probably a day of the month. the count "scannedHours"
	// includes ourselves
	// . mdw: i made it scannedHours>=1 from scannedHours>1 to fix a
	//   trumba.com rss page date of "Friday, December 4, 1pm"
	if ( monthPreceeds && ! hadMin && scannedHours >= 1 && 
	     // fix August 27, 5-8 p.m.
	     ! hadRangeIndicator )
		return -2;

	// fix "9/20 3:00p" for www.when.com/albuquerque-nm/venues
	if ( scannedHours >= 3 && ! hadMin )
		return -2;

	// update. but do not include any other numbers in our string!
	// we need to allow the logic above to add in DT_RANGE_TOD date types
	// and not do it here
	//if ( gotIt && ampmFollows && scannedHours == 0 ) {
	//	// sanity check
	//	if ( additional < 0 ) { char *xx=NULL;*xx=0; }
	//	*endWordNum += additional + hadPunct - 1;
	//	// MDW hack fix for "4A"
	//	if ( *endWordNum == i ) *endWordNum = i+1;
	//}

	// we REQUIRE an am or pm, OR a MINUTE
	if ( ! gotIt && ! hadMin ) return -2;
	
	// . add in 12 hours if we are pm
	// . crap, for "12 midnight" hour was already set to 24 above
	if ( isPM && hour != 12 && hour != 24 ) hour += 12;
	
	// . if we had "8:30" but no am or pm, assume "pm"
	// . do not do this if in military time though! (hour>=12)
	// . no, this messes up "Daily 9-5:30"
	//if ( ! isAM && ! isPM && hadMin && hour < 12 ) hour += 12;

	// . 12 am (midnight) is an exception
	// . CAUTION: this exceeds the 24 hr clock!
	if ( isAM && hour == 12 ) hour += 12;

	// away with crap like "15 pm" (which gets made to hour 27)
	if ( hour >= 25 ) return -2;

	// 24:10 is not a valid time
	//if ( hour == 24 && minute != 0 ) return -2;
	
	// . if we had something, advance "i" over the "am" or "pm"
	// . update i to point to where "s" left off
	while ( gotIt && i+1 < nw && wptrs[i] < s ) i++;
	// otherwise, just skip the hour/min/sec that we were ref'ing
	if ( ! gotIt ) i++;
	
	// . point to possible timezone
	// . if we are on a punctuation word, skip that
	if ( ! wids[i] && ! tids[i] ) i++;
	
	// for ceder.net's "6:30 - 8.00PM" fix!
	if ( hadPeriod && ! isAM && ! isPM && ! isMil ) return -2;

	// now we should be pointing to the timezone
	*tzPtr = NULL;
	// skip if word is "at", not a good timezone!
	//int64_t h_at = hash64("at",2);
	// tzptr will be set to NULL if not recognized as a timezone
	int32_t tznw = 0;
	if ( wids[i] ) {
		tznw = getTimeZoneWord ( i , wids, nw,tzPtr , m_niceness );
		// return -1 with g_errno set on error
		if ( tznw < 0 ) return -1;
		// sanity
		if ( tznw >= 25 ) { char *xx=NULL;*xx=0; }
	}
	// advance i over timezone, if we had one
	if ( *tzPtr ) {
		// sanity check
		if ( tznw <= 0 ) { char *xx=NULL;*xx=0; }
		// . update this too now!
		// . tznw should be like 1 or 3, etc.
		// . watch out for 9-5 EST, do not update end of "9" to EST
		if ( followingNum < 0 ) *endWordNum = i + tznw;
		// skip over it for next time
		i++;
	}
	
	// make it
	int32_t seconds = hour * 3600 + minute * 60;
	// sanity check
	if ( seconds < 0 ) { char *xx=NULL;*xx=0; }
	// sanity check
	if ( seconds > 25*3600 ) { char *xx=NULL;*xx=0; }

	if ( isAM || isPM || isMil ) *hadAMPM = true;
	else                         *hadAMPM = false;
	if ( *isMilitary ) return seconds;
	if ( *hadAMPM    ) return seconds;

	if ( m_contentType != CT_XML ) return seconds;


	///////////
	//
	// BEGIN XML MILITARY TAG TIME CHECK
	//
	///////////
	int32_t kmin = i - 20;
	if ( kmin < 0 ) kmin = 0;
	int32_t k = i - 1;
	bool hitLeftTag = false;
	for ( ; k >= kmin ; k-- ) {
		// stop on tag word
		if ( tids[k] ) { hitLeftTag = true; break; }
		// skip punct words
		if ( ! wids[k] ) continue;
		// stop on alnum
		if ( ! is_digit(wptrs[k][0]) ) break;
		// make sure all are digits
		if ( ! m_words->isNum ( k ) ) break;
		// ok, it was just pure numbers, keep going
	}
	// if we hit a tag on the left, check our right
	k = i + 1;
	kmax = i + 20;
	bool hitRightTag = false;
	if ( ! hitLeftTag )  k = kmax;
	for ( ; k < kmax ; k-- ) {
		// stop on tag word
		if ( tids[k] ) { hitRightTag = true; break; }
		// skip punct words
		if ( ! wids[k] ) continue;
		// stop on alnum
		if ( ! is_digit(wptrs[k][0]) ) break;
		// make sure all are digits
		if ( ! m_words->isNum ( k ) ) break;
		// ok, it was just pure numbers, keep going
	}
	// if we are an isolated pure number in an xml tag, assume military
	// if we did not have an am/pm
	if ( ! hitLeftTag  ) return seconds;
	if ( ! hitRightTag ) return seconds;
	*isMilitary = true;
	///////////
	//
	// END XML MILITARY TAG TIME CHECK
	//
	///////////



	// return it
	return seconds;
}


TimeZone tzs[] = {
	{ "acdt"    ,  10,  30, 1 }, //  ACDT, +10:30
	{ "acst"    ,   9,  30, 1 }, //  ACST, +9:30
	{ "adt"     ,  -3,   0, 1 }, //  ADT, -3:00
	{ "aedt"    ,  11,   0, 1 }, //  AEDT, +11:00
	{ "aest"    ,  10,   0, 1 }, //  AEST, +10:00
	{ "aft"     ,   4,  30, 1 }, //  AFT, +4:30
	{ "ahdt"    ,  -9,   0, 1 }, //  AHDT, -9:00 - historical?
	{ "ahst"    , -10,   0, 1 }, //  AHST, -10:00 - historical?
	{ "akdt"    ,  -8,   0, 1 }, //  AKDT, -8:00
	{ "akst"    ,  -9,   0, 1 }, //  AKST, -9:00
	{ "amst"    ,   4,   0, 1 }, //  AMST, +4:00
	{ "amt"     ,   4,   0, 1 }, //  AMT, +4:00
	{ "anast"   ,  13,   0, 1 }, //  ANAST, +13:00
	{ "anat"    ,  12,   0, 1 }, //  ANAT, +12:00
	{ "art"     ,  -3,   0, 1 }, //  ART, -3:00
	{ "ast"     ,  -4,   0, 1 }, //  AST, -4:00
	{ "at"      ,  -1,   0, 1 }, //  AT, -1:00
	{ "awst"    ,   8,   0, 1 }, //  AWST, +8:00
	{ "azost"   ,   0,   0, 1 }, //  AZOST, 0:00
	{ "azot"    ,  -1,   0, 1 }, //  AZOT, -1:00
	{ "azst"    ,   5,   0, 1 }, //  AZST, +5:00
	{ "azt"     ,   4,   0, 1 }, //  AZT, +4:00
	{ "badt"    ,   4,   0, 1 }, //  BADT, +4:00
	{ "bat"     ,   6,   0, 1 }, //  BAT, +6:00
	{ "bdst"    ,   2,   0, 1 }, //  BDST, +2:00
	{ "bdt"     ,   6,   0, 1 }, //  BDT, +6:00
	{ "bet"     , -11,   0, 1 }, //  BET, -11:00
	{ "bnt"     ,   8,   0, 1 }, //  BNT, +8:00
	{ "bort"    ,   8,   0, 1 }, //  BORT, +8:00
	{ "bot"     ,  -4,   0, 1 }, //  BOT, -4:00
	{ "bra"     ,  -3,   0, 1 }, //  BRA, -3:00
	{ "bst"     ,   1,   0, 1 }, //  BST, +1:00
	{ "bt"      ,   6,   0, 1 }, //  BT, +6:00
	{ "btt"     ,   6,   0, 1 }, //  BTT, +6:00
	{ "cat"     ,   2,   0, 1 }, //  CAT, +2:00
	{ "cct"     ,   8,   0, 1 }, //  CCT, +8:00
	{ "cdt"     ,  -5,   0, 1 }, //  CDT, -5:00
	{ "cest"    ,   2,   0, 1 }, //  CEST, +2:00
	{ "cet"     ,   1,   0, 1 }, //  CET, +1:00
	{ "chadt"   ,  13,  45, 1 }, //  CHADT, +13:45
	{ "chast"   ,  12,  45, 1 }, //  CHAST, +12:45
	{ "chst"    ,  10,   0, 1 }, //  CHST, +10:00
	{ "ckt"     , -10,   0, 1 }, //  CKT, -10:00
	{ "clst"    ,  -3,   0, 1 }, //  CLST, -3:00
	{ "clt"     ,  -4,   0, 1 }, //  CLT, -4:00
	{ "cot"     ,  -5,   0, 1 }, //  COT, -5:00
	{ "cst"     ,  -6,   0, 1 }, //  CST, -6:00
	{ "ct"      ,  -6,   0, 1 }, //  CT, -6:00
	{ "cut"     ,   0,   0, 2 }, //  CUT, 0:00
	{ "cxt"     ,   7,   0, 1 }, //  CXT, +7:00
	{ "davt"    ,   7,   0, 1 }, //  DAVT, +7:00
	{ "ddut"    ,  10,   0, 1 }, //  DDUT, +10:00
	{ "dnt"     ,   1,   0, 1 }, //  DNT, +1:00
	{ "dst"     ,   2,   0, 1 }, //  DST, +2:00
	{ "easst"   ,  -5,   0, 1 }, //  EASST -5:00
	{ "east"    ,  -6,   0, 1 }, //  EAST, -6:00
	{ "eat"     ,   3,   0, 1 }, //  EAT, +3:00
	{ "ect"     ,  -5,   0, 1 }, //  ECT, -5:00
	{ "edt"     ,  -4,   0, 1 }, //  EDT, -4:00
	{ "eest"    ,   3,   0, 1 }, //  EEST, +3:00
	{ "eet"     ,   2,   0, 1 }, //  EET, +2:00
	{ "egst"    ,   0,   0, 1 }, //  EGST, 0:00
	{ "egt"     ,  -1,   0, 1 }, //  EGT, -1:00
	{ "emt"     ,   1,   0, 1 }, //  EMT, +1:00
	{ "est"     ,  -5,   0, 1 }, //  EST, -5:00
	{ "et"      ,  -5,   0, 1 }, //  ET, -5:00
	{ "fdt"     ,  -1,   0, 1 }, //  FDT, -1:00
	{ "fjst"    ,  13,   0, 1 }, //  FJST, +13:00
	{ "fjt"     ,  12,   0, 1 }, //  FJT, +12:00
	{ "fkst"    ,  -3,   0, 1 }, //  FKST, -3:00
	{ "fkt"     ,  -4,   0, 1 }, //  FKT, -4:00
	{ "fst"     ,   2,   0, 1 }, //  FST, +2:00
	{ "fwt"     ,   1,   0, 1 }, //  FWT, +1:00
	{ "galt"    ,  -6,   0, 1 }, //  GALT, -6:00
	{ "gamt"    ,  -9,   0, 1 }, //  GAMT, -9:00
	{ "gest"    ,   5,   0, 1 }, //  GEST, +5:00
	{ "get"     ,   4,   0, 1 }, //  GET, +4:00
	{ "gft"     ,  -3,   0, 1 }, //  GFT, -3:00
	{ "gilt"    ,  12,   0, 1 }, //  GILT, +12:00
	{ "gmt"     ,   0,   0, 2 }, //  GMT, 0:00
	{ "gst"     ,  10,   0, 1 }, //  GST, +10:00
	{ "gt"      ,   0,   0, 2 }, //  GT, 0:00
	{ "gyt"     ,  -4,   0, 1 }, //  GYT, -4:00
	{ "gz"      ,   0,   0, 2 }, //  GZ, 0:00
	{ "haa"     ,  -3,   0, 1 }, //  HAA, -3:00
	{ "hac"     ,  -5,   0, 1 }, //  HAC, -5:00
	{ "hae"     ,  -4,   0, 1 }, //  HAE, -4:00
	{ "hap"     ,  -7,   0, 1 }, //  HAP, -7:00
	{ "har"     ,  -6,   0, 1 }, //  HAR, -6:00
	{ "hat"     ,  -2, -30, 1 }, //  HAT, -2:30
	{ "hay"     ,  -8,   0, 1 }, //  HAY, -8:00
	{ "hdt"     ,  -9, -30, 1 }, //  HDT, -9:30
	{ "hfe"     ,   2,   0, 1 }, //  HFE, +2:00
	{ "hfh"     ,   1,   0, 1 }, //  HFH, +1:00
	{ "hg"      ,   0,   0, 2 }, //  HG, 0:00
	{ "hkt"     ,   8,   0, 1 }, //  HKT, +8:00
	{ "hna"     ,  -4,   0, 1 }, //  HNA, -4:00
	{ "hnc"     ,  -6,   0, 1 }, //  HNC, -6:00
	{ "hne"     ,  -5,   0, 1 }, //  HNE, -5:00
	{ "hnp"     ,  -8,   0, 1 }, //  HNP, -8:00
	{ "hnr"     ,  -7,   0, 1 }, //  HNR, -7:00
	{ "hnt"     ,  -3, -30, 1 }, //  HNT, -3:30
	{ "hny"     ,  -9,   0, 1 }, //  HNY, -9:00
	{ "hoe"     ,   1,   0, 1 }, //  HOE, +1:00
	{ "hours"   ,   0,   0, 2 }, //  HOURS, no change, but indicates time
	{ "hrs"     ,   0,   0, 2 }, //  HRS, no change, but indicates time
	{ "hst"     , -10,   0, 1 }, //  HST, -10:00
	{ "ict"     ,   7,   0, 1 }, //  ICT, +7:00
	{ "idle"    ,  12,   0, 1 }, //  IDLE, +12:00
	{ "idlw"    , -12,   0, 1 }, //  IDLW, -12:00
	{ "idt"     ,   3,   0, 1 }, //  IDT, +3:00
	{ "iot"     ,   5,   0, 1 }, //  IOT, +5:00
	{ "irdt"    ,   4,  30, 1 }, //  IRDT, +4:30
	{ "irkst"   ,   9,   0, 1 }, //  IRKST, +9:00
	{ "irkt"    ,   8,   0, 1 }, //  IRKT, +8:00
	{ "irst"    ,   4,  30, 1 }, //  IRST, +4:30
	{ "irt"     ,   3,  30, 1 }, //  IRT, +3:30
	{ "ist"     ,   1,   0, 1 }, //  IST, +1:00
	{ "it"      ,   3,  30, 1 }, //  IT, +3:30
	{ "ita"     ,   1,   0, 1 }, //  ITA, +1:00
	{ "javt"    ,   7,   0, 1 }, //  JAVT, +7:00
	{ "jayt"    ,   9,   0, 1 }, //  JAYT, +9:00
	{ "jst"     ,   9,   0, 1 }, //  JST, +9:00
	{ "jt"      ,   7,   0, 1 }, //  JT, +7:00
	{ "kdt"     ,  10,   0, 1 }, //  KDT, +10:00
	{ "kgst"    ,   6,   0, 1 }, //  KGST, +6:00
	{ "kgt"     ,   5,   0, 1 }, //  KGT, +5:00
	{ "kost"    ,  12,   0, 1 }, //  KOST, +12:00
	{ "krast"   ,   8,   0, 1 }, //  KRAST, +8:00
	{ "krat"    ,   7,   0, 1 }, //  KRAT, +7:00
	{ "kst"     ,   9,   0, 1 }, //  KST, +9:00
	{ "lhdt"    ,  11,   0, 1 }, //  LHDT, +11:00
	{ "lhst"    ,  10,  30, 1 }, //  LHST, +10:30
	{ "ligt"    ,  10,   0, 1 }, //  LIGT, +10:00
	{ "lint"    ,  14,   0, 1 }, //  LINT, +14:00
	{ "lkt"     ,   6,   0, 1 }, //  LKT, +6:00
	{ "magst"   ,  12,   0, 1 }, //  MAGST, +12:00
	{ "magt"    ,  11,   0, 1 }, //  MAGT, +11:00
	{ "mal"     ,   8,   0, 1 }, //  MAL, +8:00
	{ "mart"    ,  -9, -30, 1 }, //  MART, -9:30
	{ "mat"     ,   3,   0, 1 }, //  MAT, +3:00
	{ "mawt"    ,   6,   0, 1 }, //  MAWT, +6:00
	{ "mdt"     ,  -6,   0, 1 }, //  MDT, -6:00
	{ "med"     ,   2,   0, 1 }, //  MED, +2:00
	{ "medst"   ,   2,   0, 1 }, //  MEDST, +2:00
	{ "mest"    ,   2,   0, 1 }, //  MEST, +2:00
	{ "mesz"    ,   2,   0, 1 }, //  MESZ, +2:00
	{ "met"     ,   1,   0, 1 }, //  MEZ, +1:00
	{ "mewt"    ,   1,   0, 1 }, //  MEWT, +1:00
	{ "mex"     ,  -6,   0, 1 }, //  MEX, -6:00
	{ "mht"     ,  12,   0, 1 }, //  MHT, +12
	{ "mmt"     ,   6,  30, 1 }, //  MMT, +6:30
	{ "mpt"     ,  10,   0, 1 }, //  MPT, +10:00
	{ "msd"     ,   4,   0, 1 }, //  MSD, +4:00
	{ "msk"     ,   3,   0, 1 }, //  MSK, +3:00
	{ "msks"    ,   4,   0, 1 }, //  MSKS, +4:00
	{ "mst"     ,  -7,   0, 1 }, //  MST, -7:00
	//{ "mt"      ,   8,  30, 1 }, // MT, +8:30
	{ "mt"      ,  -7,   0, 1 }, // MORE LIKELY MOUNTAIN TIME, -7:00
	{ "mut"     ,   4,   0, 1 }, //  MUT, +4:00
	{ "mvt"     ,   5,   0, 1 }, //  MVT, +5:00
	{ "myt"     ,   8,   0, 1 }, //  MYT, +8:00
	{ "nct"     ,  11,   0, 1 }, //  NCT, +11:00
	{ "ndt"     ,   2,  30, 1 }, //  NDT, +2:30
	{ "nft"     ,  11,  30, 1 }, //  NFT, +11:30
	{ "nor"     ,   1,   0, 1 }, //  NOR, +1:00
	{ "novst"   ,   7,   0, 1 }, //  NOVST, +7:00
	{ "novt"    ,   6,   0, 1 }, //  NOVT, +6:00
	{ "npt"     ,   5,  45, 1 }, //  NPT, +5:45
	{ "nrt"     ,  12,   0, 1 }, //  NRT, +12:00
	{ "nst"     ,  -3, -30, 1 }, //  NST, -3:30
	{ "nsut"    ,   6,  30, 1 }, //  NSUT, +6:30
	{ "nt"      , -11,   0, 1 }, //  NT, -11:00
	{ "nut"     , -11,   0, 1 }, //  NUT, -11:00
	{ "nzdt"    ,  13,   0, 1 }, //  NZDT, +13:00
	{ "nzst"    ,  12,   0, 1 }, //  NZST, +12:00
	{ "nzt"     ,  12,   0, 1 }, //  NZT, +12:00
	{ "oesz"    ,   3,   0, 1 }, //  OESZ, +3:00
	{ "oez"     ,   2,   0, 1 }, //  OEZ, +2:00
	{ "omsst"   ,   7,   0, 1 }, //  OMSST, +7:00
	{ "omst"    ,   6,   0, 1 }, //  OMST, +6:00
	{ "pdt"     ,  -7,   0, 1 }, //  PDT, -7:00
	{ "pet"     ,  -5,   0, 1 }, //  PET, -5:00
	{ "petst"   ,  13,   0, 1 }, //  PETST, +13:00
	{ "pett"    ,  12,   0, 1 }, //  PETT, +12:00
	{ "pgt"     ,  10,   0, 1 }, //  PGT, +10:00
	{ "phot"    ,  13,   0, 1 }, //  PHOT, +13:00
	{ "pht"     ,   8,   0, 1 }, //  PHT, +8:00
	{ "pkt"     ,   5,   0, 1 }, //  PKT, +5:00
	{ "pmdt"    ,  -2,   0, 1 }, //  PMDT, -2:00
	{ "pmt"     ,  -3,   0, 1 }, //  PMT, -3:00
	{ "pnt"     ,  -8, -30, 1 }, //  PNT, -8:30
	{ "pont"    ,  11,   0, 1 }, //  PONT, +11:00
	{ "pst"     ,  -8,   0, 1 }, //  PST, -8:00
	{ "pt"      ,  -8,   0, 1 }, //  PT, -8:00
	{ "pwt"     ,   9,   0, 1 }, //  PWT, +9:00
	{ "pyst"    ,  -3,   0, 1 }, //  PYST, -3:00
	{ "pyt"     ,  -4,   0, 1 }, //  PYT, -4:00
	{ "r1t"     ,   2,   0, 1 }, //  R1T, +2:00
	{ "r2t"     ,   3,   0, 1 }, //  R2T, +3:00
	{ "ret"     ,   4,   0, 1 }, //  RET, +4:00
	{ "rok"     ,   9,   0, 1 }, //  ROK, +9:00
	{ "sadt"    ,  10,  30, 1 }, //  SADT, +10:30
	{ "sast"    ,   2,   0, 1 }, //  SAST, +2:00
	{ "sbt"     ,  11,   0, 1 }, //  SBT, +11:00
	{ "sct"     ,   4,   0, 1 }, //  SCT, +4:00
	{ "set"     ,   1,   0, 1 }, //  SET, +1:00
	{ "sgt"     ,   8,   0, 1 }, //  SGT, +8:00
	{ "srt"     ,  -3,   0, 1 }, //  SRT, -3:00
	{ "sst"     ,   2,   0, 1 }, //  SST, +2:00
	{ "swt"     ,   1,   0, 1 }, //  SWT, +1:00
	{ "tft"     ,   5,   0, 1 }, //  TFT,  +5:00
	{ "tha"     ,   7,   0, 1 }, //  THA, +7:00
	{ "that"    , -10,   0, 1 }, //  THAT, -10:00
	{ "tjt"     ,   5,   0, 1 }, //  TJT, +5:00
	{ "tkt"     , -10,   0, 1 }, //  TKT, -10:00
	{ "tmt"     ,   5,   0, 1 }, //  TMT, +5:00
	{ "tot"     ,  13,   0, 1 }, //  TOT, +13:00
	{ "truk"    ,  10,   0, 1 }, //  TRUK, +10:00
	{ "tst"     ,   3,   0, 1 }, //  TST, +3:00
	{ "tuc"     ,   0,   0, 1 }, //  TUC, 0:00
	{ "tvt"     ,  12,   0, 1 }, //  TVT, 12:00
	{ "ulast"   ,   9,   0, 1 }, //  ULAST, +9:00
	{ "ulat"    ,   8,   0, 1 }, //  ULAT, +8:00
	{ "usz1"    ,   2,   0, 1 }, //  USZ1, +2:00
	{ "usz1s"   ,   3,   0, 1 }, //  USZ1S, +3:00
	{ "usz2"    ,   3,   0, 1 }, //  USZ2, +3:00
	{ "usz2s"   ,   4,   0, 1 }, //  USZ2S, +4:00
	{ "usz3"    ,   4,   0, 1 }, //  USZ3, +4:00
	{ "usz3s"   ,   5,   0, 1 }, //  USZ3S, +5:00
	{ "usz4"    ,   5,   0, 1 }, //  USZ4, +5:00
	{ "usz4s"   ,   6,   0, 1 }, //  USZ4S, +6:00
	{ "usz5"    ,   6,   0, 1 }, //  USZ5, +6:00
	{ "usz5s"   ,   7,   0, 1 }, //  USZ5S, +7:00
	{ "usz6"    ,   7,   0, 1 }, //  USZ6, +7:00
	{ "usz6s"   ,   8,   0, 1 }, //  USZ6S, +8:00
	{ "usz7"    ,   8,   0, 1 }, //  USZ7, +8:00
	{ "usz7s"   ,   9,   0, 1 }, //  USZ7S, +9:00
	{ "usz8"    ,   9,   0, 1 }, //  USZ8, +9:00
	{ "usz8s"   ,  10,   0, 1 }, //  USZ8S, +10:00
	{ "usz9"    ,  10,   0, 1 }, //  USZ9, +10:00
	{ "usz9s"   ,  11,   0, 1 }, //  USZ9S, +11:00
	{ "utc"     ,   0,   0, 2 }, //  UTC, 0:00
	{ "utz"     ,  -3,   0, 1 }, //  UTZ, -3:00
	{ "uyt"     ,  -3,   0, 1 }, //  UYT, -3:00
	{ "uz10"    ,  11,   0, 1 }, //  UZ10, +11:00
	{ "uz10s"   ,  12,   0, 1 }, //  UZ10S, +12:00
	{ "uz11"    ,  12,   0, 1 }, //  UZ11, +12:00
	{ "uz11s"   ,  13,   0, 1 }, //  UZ11S, +13:00
	{ "uz12"    ,  13,   0, 1 }, //  UZ12, +13:00
	{ "uz12s"   ,  14,   0, 1 }, //  UZ12S, +14:00
	{ "uzt"     ,   5,   0, 1 }, //  UZT, +5:00
	{ "vet"     ,  -4,   0, 1 }, //  VET, -4:00
	{ "vlast"   ,  11,   0, 1 }, //  VLAST, +11:00
	{ "vlat"    ,  10,   0, 1 }, //  VLAT, +10:00
	{ "vtz"     ,  -2,   0, 1 }, //  VTZ, -2:00
	{ "vut"     ,  11,   0, 1 }, //  VUT, +11:00
	{ "wakt"    ,  12,   0, 1 }, //  WAKT, +12:00
	{ "wast"    ,   2,   0, 1 }, //  WAST, +2:00
	{ "wat"     ,   1,   0, 1 }, //  WAT, +1:00
	{ "west"    ,   1,   0, 1 }, //  WEST, +1:00
	{ "wesz"    ,   1,   0, 1 }, //  WESZ, +1:00
	{ "wet"     ,   0,   0, 1 }, //  WET, 0:00
	{ "wez"     ,   0,   0, 1 }, //  WEZ, 0:00
	{ "wft"     ,  12,   0, 1 }, //  WFT, +12:00
	{ "wgst"    ,  -2,   0, 1 }, //  WGST, -2:00
	{ "wgt"     ,  -3,   0, 1 }, //  WGT, -3:00
	{ "wib"     ,   7,   0, 1 }, //  WIB, +7:00
	{ "wit"     ,   9,   0, 1 }, //  WIT, +9:00
	{ "wita"    ,   8,   0, 1 }, //  WITA, +8:00
	{ "wst"     ,   8,   0, 1 }, //  WST, +8:00
	{ "wtz"     ,  -1,   0, 1 }, //  WTZ, -1:00
	{ "wut"     ,   1,   0, 1 }, //  WUT, 1:00
	{ "yakst"   ,  10,   0, 1 }, //  YAKST, +10:00
	{ "yakt"    ,   9,   0, 1 }, //  YAKT, +9:00
	{ "yapt"    ,  10,   0, 1 }, //  YAPT, +10:00
	{ "ydt"     ,  -8,   0, 1 }, //  YDT, -8:00
	{ "yekst"   ,   6,   0, 1 }, //  YEKST, +6:00
	{ "yst"     ,  -9,   0, 1 }, //  YST, -9:00
	{ "\0"      ,   0,   0, 0 } };

// hash table of timezone information
static HashTableX s_tzt;

static int64_t h_mountain;
static int64_t h_eastern;
static int64_t h_central;
static int64_t h_pacific;
static int64_t h_time2;
static int64_t h_mdt;
static int64_t h_at2;

bool initTimeZoneTable ( ) {

	// if already initalized return true
	if ( s_tzt.m_numSlotsUsed ) return true;

	// init static wids
	h_mountain = hash64n("mountain");
	h_eastern  = hash64n("eastern");
	h_central  = hash64n("central");
	h_pacific  = hash64n("pacific");
	h_time2    = hash64n("time");
	h_mdt      = hash64n("mdt");
	h_at2      = hash64n("at");
	// set up the time zone hashtable
	if ( ! s_tzt.set( 8,4, 300,NULL,0,false,0,"tzts"))
		return false;
	// load time zone names and their modifiers into hashtable
	for ( int32_t i = 0 ; *tzs[i].m_name ; i++ ) {
		char *t    = tzs[i].m_name;
		int32_t  tlen = gbstrlen(t);
		// hash like Words.cpp computeWordIds
		uint64_t h    = hash64Lower_utf8( t , tlen );
		// use the ptr as the value
		if ( ! s_tzt.addKey ( &h, &tzs[i] ) )
			return false;
	}
	return true;
}

// return what we have to add to UTC to get time in locale specified by "s"
// where "s" is like "PDT" "MST" "EST" etc. if unknown return 999999
int32_t getTimeZone ( char *s ) {
	if ( ! s ) return BADTIMEZONE;
	char *send = s;
	// point to end of the potential timezone
	for ( ; *send && isalnum(*send) ; send++ );
	// hash it
	uint64_t h = hash64Lower_utf8( s , send -s );
	// make sure table is ready
	initTimeZoneTable();
	// look it up
	int32_t slot = s_tzt.getSlot( &h );
	if ( slot < 0 ) return 999999;
	// did we find it in the table?
	TimeZone *tzptr = (TimeZone *)s_tzt.getValueFromSlot ( slot );
	// no error, return true
	int32_t secs = tzptr->m_hourMod * 3600;
	secs += tzptr->m_minMod * 60;
	return secs;
}

// . returns how many words starting at i are in the time zone
// . 0 means not a timezone
int32_t getTimeZoneWord ( int32_t i ,
		       int64_t *wids, 
		       int32_t nw ,
		       TimeZone **tzptr , 
		       int32_t niceness ) {

	// no ptr
	*tzptr = NULL;
	// only init table once
	bool s_init16 = false;
	// init the hash table of month names
	if ( ! s_init16 ) {
		// on error we return -1 from here
		if ( ! initTimeZoneTable() ) return -1;
		s_init16 = true;
	}
	// this is too common of a word!
	if ( wids[i] == h_at2 ) return 0;

	int32_t slot = s_tzt.getSlot( &wids[i] );
	// return this, assume just one word
	int32_t tznw = 1;
	// . "mountain time"
	// . this removes the event title "M-F 8:30 AM-5:30 PM Mountain Time"
	//   from the event (horus) on http://www.sfreporter.com/contact_us/
	if ( slot<0 && i+2<nw && wids[i+2] == h_time2 ) {
		if ( wids[i] == h_mountain ) {
			slot = s_tzt.getSlot (&h_mdt);
			tznw = 3;
		}
		if ( wids[i] == h_eastern ) {
			slot = s_tzt.getSlot (&h_eastern);
			tznw = 3;
		}
		if ( wids[i] == h_central ) {
			slot = s_tzt.getSlot (&h_central);
			tznw = 3;
		}
		if ( wids[i] == h_pacific ) {
			slot = s_tzt.getSlot (&h_pacific);
			tznw = 3;
		}
	}
	// if nothing return 0
	if ( slot <0 ) return 0;
	// did we find it in the table?
	*tzptr = (TimeZone *)s_tzt.getValueFromSlot ( slot );
	// no error, return true
	return tznw;
}

#define MAX_WIDS 5

struct DateVal {
	char       m_str[32];
	datetype_t m_type;
	int32_t       m_val;
	char       m_numWids;
	int64_t  m_wids[MAX_WIDS];
};

struct DateVal dvs[] = {

	{ "sunday"    , DT_DOW , 1,0,{0,0,0,0,0}},
	{ "monday"    , DT_DOW , 2,0,{0,0,0,0,0}},
	{ "tuesday"   , DT_DOW , 3,0,{0,0,0,0,0}},
	{ "thuesday"  , DT_DOW , 3,0,{0,0,0,0,0}}, // misspelling
	{ "wednesday" , DT_DOW , 4,0,{0,0,0,0,0}},
	{ "thursday"  , DT_DOW , 5,0,{0,0,0,0,0}},
	{ "friday"    , DT_DOW , 6,0,{0,0,0,0,0}},
	{ "saturday"  , DT_DOW , 7,0,{0,0,0,0,0}},

	{ "sundays"    , DT_DOW , 1,0,{0,0,0,0,0}},
	{ "mondays"    , DT_DOW , 2,0,{0,0,0,0,0}},
	{ "tuesdays"   , DT_DOW , 3,0,{0,0,0,0,0}},
	{ "wednesdays" , DT_DOW , 4,0,{0,0,0,0,0}},
	{ "thursdays"  , DT_DOW , 5,0,{0,0,0,0,0}},
	{ "fridays"    , DT_DOW , 6,0,{0,0,0,0,0}},
	{ "saturdays"  , DT_DOW , 7,0,{0,0,0,0,0}},

	// http://www.rateclubs.com/clubs/catch-one_14445.html has
	// "every 1st & 2nd Thursday's"
	{ "sunday's"    , DT_DOW , 1,0,{0,0,0,0,0}},
	{ "monday's"    , DT_DOW , 2,0,{0,0,0,0,0}},
	{ "tuesday's"   , DT_DOW , 3,0,{0,0,0,0,0}},
	{ "wednesday's" , DT_DOW , 4,0,{0,0,0,0,0}},
	{ "thursday's"  , DT_DOW , 5,0,{0,0,0,0,0}},
	{ "friday's"    , DT_DOW , 6,0,{0,0,0,0,0}},
	{ "saturday's"  , DT_DOW , 7,0,{0,0,0,0,0}},

	// for "m-f" for http://www.sfreporter.com/contact_us/
	{ "su"     , DT_DOW, 1,0,{0,0,0,0,0}}, // tu - su
	{ "m"      , DT_DOW, 2,0,{0,0,0,0,0}},
	{ "mo"     , DT_DOW, 2,0,{0,0,0,0,0}}, // mo-fr
	{ "t"      , DT_DOW, 3,0,{0,0,0,0,0}}, // T-F
	{ "tu"     , DT_DOW, 3,0,{0,0,0,0,0}},
	{ "th"     , DT_DOW, 5,0,{0,0,0,0,0}},
	{ "f"      , DT_DOW, 6,0,{0,0,0,0,0}},
	{ "fr"     , DT_DOW, 6,0,{0,0,0,0,0}},
	{ "sa"     , DT_DOW, 7,0,{0,0,0,0,0}},

	{ "sun"    , DT_DOW , 1,0,{0,0,0,0,0}},
	{ "mon"    , DT_DOW , 2,0,{0,0,0,0,0}},
	{ "tue"    , DT_DOW , 3,0,{0,0,0,0,0}},
	{ "wed"    , DT_DOW , 4,0,{0,0,0,0,0}},
	{ "weds"   , DT_DOW , 4,0,{0,0,0,0,0}},
	{ "thu"    , DT_DOW , 5,0,{0,0,0,0,0}},
	{ "fri"    , DT_DOW , 6,0,{0,0,0,0,0}},
	{ "sat"    , DT_DOW , 7,0,{0,0,0,0,0}},

	{ "tues"   , DT_DOW , 3,0,{0,0,0,0,0}},
	{ "thur"   , DT_DOW , 5,0,{0,0,0,0,0}},
	{ "thurs"  , DT_DOW , 5,0,{0,0,0,0,0}},
	{ "thr"    , DT_DOW , 5,0,{0,0,0,0,0}},
	{ "thursay", DT_DOW , 5,0,{0,0,0,0,0}},
	{ "thurday", DT_DOW , 5,0,{0,0,0,0,0}},
	// fix http://www.sfperformingarts.org/classes/tmp-class1
	{ "thrusdays"  , DT_DOW , 5,0,{0,0,0,0,0}},
	


	{ "january"       , DT_MONTH, 1 ,0,{0,0,0,0,0}}, 
	{ "february"      , DT_MONTH, 2 ,0,{0,0,0,0,0}},
	{ "march"         , DT_MONTH, 3 ,0,{0,0,0,0,0}},
	{ "april"         , DT_MONTH, 4 ,0,{0,0,0,0,0}},
	{ "may"           , DT_MONTH, 5 ,0,{0,0,0,0,0}},
	{ "june"          , DT_MONTH,  6 ,0,{0,0,0,0,0}},
	{ "july"          , DT_MONTH, 7 ,0,{0,0,0,0,0}},
	{ "august"        , DT_MONTH, 8 ,0,{0,0,0,0,0}},
	{ "september"     , DT_MONTH, 9  ,0,{0,0,0,0,0}},
	{ "septemer"     , DT_MONTH, 9  ,0,{0,0,0,0,0}}, // misspelling
	{ "october"       , DT_MONTH, 10 ,0,{0,0,0,0,0}},
	{ "november"      , DT_MONTH, 11 ,0,{0,0,0,0,0}},
	{ "december"      , DT_MONTH, 12 ,0,{0,0,0,0,0}},
	
	
	{ "jan"           , DT_MONTH, 1 ,0,{0,0,0,0,0}},
	{ "feb"           , DT_MONTH, 2 ,0,{0,0,0,0,0}},
	{ "mar"           , DT_MONTH, 3 ,0,{0,0,0,0,0}},
	{ "apr"           , DT_MONTH, 4 ,0,{0,0,0,0,0}},
	{ "may"           , DT_MONTH, 5 ,0,{0,0,0,0,0}},
	{ "jun"           , DT_MONTH, 6 ,0,{0,0,0,0,0}},
	{ "jul"           , DT_MONTH, 7 ,0,{0,0,0,0,0}},
	{ "aug"           , DT_MONTH, 8 ,0,{0,0,0,0,0}},
	{ "sep"           , DT_MONTH, 9  ,0,{0,0,0,0,0}},
	{ "sept"          , DT_MONTH, 9  ,0,{0,0,0,0,0}},
	{ "oct"           , DT_MONTH, 10 ,0,{0,0,0,0,0}},
	{ "nov"           , DT_MONTH, 11 ,0,{0,0,0,0,0}},
	{ "dec"           , DT_MONTH, 12 ,0,{0,0,0,0,0}},

	{"noon"           , DT_TOD, 12*3600,0,{0,0,0,0,0}},
	{"midday"         , DT_TOD, 12*3600,0,{0,0,0,0,0}},
	{"midnight"       , DT_TOD, 24*3600,0,{0,0,0,0,0}},

	// using these as tods causes too many false positives on event
	// titles like "Cultural Sunset Thursdays", it thinks that is an
	// event brother because it has a dow and tod... so unless there
	// are some special indicators like "starts at sunset" or something
	// then assume these are creative title names.
	//{"dawn"           , DT_TOD,  6*3600,0,{0,0,0,0,0}},
	//{"sunrise"        , DT_TOD,  6*3600,0,{0,0,0,0,0}},
	//{"dusk"           , DT_TOD, 18*3600,0,{0,0,0,0,0}},
	//{"sunset"         , DT_TOD, 18*3600,0,{0,0,0,0,0}},

	//{"last"      , DT_MOD, 32,0,{0,0,0,0,0}},
	//{"every"     , DT_MOD, 32,0,{0,0,0,0,0}},
	//{"each"      , DT_MOD, 32,0,{0,0,0,0,0}},

	//{"until"     , DT_MOD, 32,0,{0,0,0,0,0}},
	//{"through"   , DT_MOD, 32,0,{0,0,0,0,0}},
	//{"thru"      , DT_MOD, 32,0,{0,0,0,0,0}},
	//{"continuing"     , DT_MOD, 32,0,{0,0,0,0,0}},

	/*
	{"first"      , DT_MOD, 1,0,{0,0,0,0,0}},
	{"second"     , DT_MOD, 2,0,{0,0,0,0,0}},
	{"third"      , DT_MOD, 3,0,{0,0,0,0,0}},
	{"fourth"     , DT_MOD,4 ,0,{0,0,0,0,0}},
	{"fifth"      , DT_MOD, 5,0,{0,0,0,0,0}},
	{"sixth"      , DT_MOD, 6,0,{0,0,0,0,0}},
	{"seventh"    , DT_MOD,7 ,0,{0,0,0,0,0}},
	{"eigth"      , DT_MOD, 8,0,{0,0,0,0,0}},
	{"ninth"      , DT_MOD, 9,0,{0,0,0,0,0}},
	{"tenth"      , DT_MOD, 10,0,{0,0,0,0,0}},
	{"eleventh"   , DT_MOD,11 ,0,{0,0,0,0,0}},
	{"twelth"     , DT_MOD, 12,0,{0,0,0,0,0}},
	{"thirteenth" , DT_MOD, 13,0,{0,0,0,0,0}},
	{"fourteenth" , DT_MOD, 14,0,{0,0,0,0,0}},
	{"fifteenth"  , DT_MOD,15 ,0,{0,0,0,0,0}},
	{"sixteenth"  , DT_MOD, 16,0,{0,0,0,0,0}},
	{"seventeenth", DT_MOD,17 ,0,{0,0,0,0,0}},
	{"eighteenth" , DT_MOD, 18,0,{0,0,0,0,0}},
	{"nineteenth" , DT_MOD, 19,0,{0,0,0,0,0}},
	{"twentieth"  , DT_MOD, 20,0,{0,0,0,0,0}},
	// . a negative value is a special singifier that more follow!
	// . i.e. "twenty-third"
	{"twenty"     , DT_MOD, -20,0,{0,0,0,0,0}},
	{"thirty"     , DT_MOD, -30,0,{0,0,0,0,0}},
	*/

	{"1st" , DT_DAYNUM,  1,0,{0,0,0,0,0}},
	{"2nd" , DT_DAYNUM,  2,0,{0,0,0,0,0}},
	{"3rd" , DT_DAYNUM,  3,0,{0,0,0,0,0}},
	{"4th" , DT_DAYNUM,  4,0,{0,0,0,0,0}},
	{"5th" , DT_DAYNUM,  5,0,{0,0,0,0,0}},
	{"6th" , DT_DAYNUM,  6,0,{0,0,0,0,0}},
	{"7th" , DT_DAYNUM,  7,0,{0,0,0,0,0}},
	{"8th" , DT_DAYNUM,  8,0,{0,0,0,0,0}},
	{"9th" , DT_DAYNUM,  9,0,{0,0,0,0,0}},
	{"10th", DT_DAYNUM, 10,0,{0,0,0,0,0}},
	{"11th", DT_DAYNUM, 11,0,{0,0,0,0,0}},
	{"12th", DT_DAYNUM, 12,0,{0,0,0,0,0}},
	{"13th", DT_DAYNUM, 13,0,{0,0,0,0,0}},
	{"14th", DT_DAYNUM, 14,0,{0,0,0,0,0}},
	{"15th", DT_DAYNUM, 15,0,{0,0,0,0,0}},
	{"16th", DT_DAYNUM, 16,0,{0,0,0,0,0}},
	{"17th", DT_DAYNUM, 17,0,{0,0,0,0,0}},
	{"18th", DT_DAYNUM, 18,0,{0,0,0,0,0}},
	{"19th", DT_DAYNUM, 19,0,{0,0,0,0,0}},
	{"20th", DT_DAYNUM, 20,0,{0,0,0,0,0}},
	{"21st", DT_DAYNUM, 21,0,{0,0,0,0,0}},
	{"22nd", DT_DAYNUM, 22,0,{0,0,0,0,0}},
	{"23rd", DT_DAYNUM, 23,0,{0,0,0,0,0}},
	{"24th", DT_DAYNUM, 24,0,{0,0,0,0,0}},
	{"25th", DT_DAYNUM, 25,0,{0,0,0,0,0}},
	{"26th", DT_DAYNUM, 26,0,{0,0,0,0,0}},
	{"27th", DT_DAYNUM, 27,0,{0,0,0,0,0}},
	{"28th", DT_DAYNUM, 28,0,{0,0,0,0,0}},
	{"29th", DT_DAYNUM, 29,0,{0,0,0,0,0}},
	{"30th", DT_DAYNUM, 30,0,{0,0,0,0,0}},
	{"31th", DT_DAYNUM, 31,0,{0,0,0,0,0}},

	// the value for holidays is a code labelling the holdiay
	{"New Year's Day",DT_HOLIDAY,HD_NEW_YEARS_DAY,0,{0,0,0,0,0}},
	{"New Years Day",DT_HOLIDAY,HD_NEW_YEARS_DAY,0,{0,0,0,0,0}},
	{"New Years's Day",DT_HOLIDAY,HD_NEW_YEARS_DAY,0,{0,0,0,0,0}},
	// like in a list like "thanksgiving, christmas and new year's days"
	// like in http://www.collectorsguide.com/ab/abmud.html
	{"New Year's Days",DT_HOLIDAY,HD_NEW_YEARS_DAY,0,{0,0,0,0,0}},
	{"Martin Luther King Jr. Day",DT_HOLIDAY,HD_MARTIN_DAY,0,{0,0,0,0,0}},
	{"Martin Luther King Day",DT_HOLIDAY,HD_MARTIN_DAY,0,{0,0,0,0,0}},
	{"Groundhog Day",DT_HOLIDAY,HD_GROUNDHOG_DAY,0,{0,0,0,0,0}},
	{"Ground hog Day",DT_HOLIDAY,HD_GROUNDHOG_DAY,0,{0,0,0,0,0}},
	{"Super Bowl Sunday",DT_HOLIDAY,HD_SUPERBOWL,0,{0,0,0,0,0}},
	{"SuperBowl Sunday",DT_HOLIDAY,HD_SUPERBOWL,0,{0,0,0,0,0}},
	{"valentine's day",DT_HOLIDAY,HD_VALENTINES,0,{0,0,0,0,0}},
	{"valentine day",DT_HOLIDAY,HD_VALENTINES,0,{0,0,0,0,0}},
	{"presidents day",DT_HOLIDAY,HD_PRESIDENTS,0,{0,0,0,0,0}},
	{"president's day",DT_HOLIDAY,HD_PRESIDENTS,0,{0,0,0,0,0}},
	{"ash wednesday",DT_HOLIDAY,HD_ASH_WEDNESDAY,0,{0,0,0,0,0}},
	{"st. patrick's day",DT_HOLIDAY,HD_ST_PATRICKS,0,{0,0,0,0,0}},
	{"st. patrick day",DT_HOLIDAY,HD_ST_PATRICKS,0,{0,0,0,0,0}},
	{"saint patrick's day",DT_HOLIDAY,HD_ST_PATRICKS,0,{0,0,0,0,0}},
	{"saint patrick day",DT_HOLIDAY,HD_ST_PATRICKS,0,{0,0,0,0,0}},
	//{"vernal equinox",DT_HOLIDAY,HD_VERNAL_EQUI,0,{0,0,0,0,0}},
	{"palm sunday",DT_HOLIDAY,HD_PALM_SUNDAY,0,{0,0,0,0,0}},
	{"first day of passover",DT_HOLIDAY,HD_FIRST_PASSOVER,0,{0,0,0,0,0}},
	{"1st day of passover",DT_HOLIDAY,HD_FIRST_PASSOVER,0,{0,0,0,0,0}},
	{"april fools day",DT_HOLIDAY,HD_APRIL_FOOLS,0,{0,0,0,0,0}},
	{"april fool's day",DT_HOLIDAY,HD_APRIL_FOOLS,0,{0,0,0,0,0}},
	{"april fool day",DT_HOLIDAY,HD_APRIL_FOOLS,0,{0,0,0,0,0}},
	{"good friday",DT_HOLIDAY,HD_GOOD_FRIDAY,0,{0,0,0,0,0}},
	{"easter sunday",DT_HOLIDAY,HD_EASTER_SUNDAY,0,{0,0,0,0,0}},
	{"easter monday",DT_HOLIDAY,HD_EASTER_MONDAY,0,{0,0,0,0,0}},
	{"easter",DT_HOLIDAY,HD_EASTER_SUNDAY,0,{0,0,0,0,0}},
	{"last day of passover",DT_HOLIDAY,HD_LAST_PASSOVER,0,{0,0,0,0,0}},
	{"patriot's day",DT_HOLIDAY,HD_PATRIOTS_DAY,0,{0,0,0,0,0}},
	{"patriot day",DT_HOLIDAY,HD_PATRIOTS_DAY,0,{0,0,0,0,0}},
	{"earth day",DT_HOLIDAY,HD_EARTH_DAY,0,{0,0,0,0,0}},
	{"secreataries day",DT_HOLIDAY,HD_SECRETARY_DAY,0,{0,0,0,0,0}},
	{"secretary's day",DT_HOLIDAY,HD_SECRETARY_DAY,0,{0,0,0,0,0}},
	{"arbor day",DT_HOLIDAY,HD_ARBOR_DAY,0,{0,0,0,0,0}},
	{"cinco de mayo",DT_HOLIDAY,HD_CINCO_DE_MAYO,0,{0,0,0,0,0}},
	{"mother's day",DT_HOLIDAY,HD_MOTHERS_DAY,0,{0,0,0,0,0}},
	{"mothers day",DT_HOLIDAY,HD_MOTHERS_DAY,0,{0,0,0,0,0}},
	{"pentecost sunday",DT_HOLIDAY,HD_PENTECOST_SUN,0,{0,0,0,0,0}},
	{"memorial day",DT_HOLIDAY,HD_MEMORIAL_DAY,0,{0,0,0,0,0}},
	{"flag day",DT_HOLIDAY,HD_FLAG_DAY,0,{0,0,0,0,0}},
	{"father's day",DT_HOLIDAY,HD_FATHERS_DAY,0,{0,0,0,0,0}},
	{"fathers day",DT_HOLIDAY,HD_FATHERS_DAY,0,{0,0,0,0,0}},
	{"summer solstice",DT_HOLIDAY,HD_SUMMER_SOL,0,{0,0,0,0,0}},
	{"independence day",DT_HOLIDAY,HD_INDEPENDENCE,0,{0,0,0,0,0}},
	{"fourth of july",DT_HOLIDAY,HD_INDEPENDENCE,0,{0,0,0,0,0}},
	{"4th of july",DT_HOLIDAY,HD_INDEPENDENCE,0,{0,0,0,0,0}},
	{"labor day",DT_HOLIDAY,HD_LABOR_DAY,0,{0,0,0,0,0}},
	{"yom kippur",DT_HOLIDAY,HD_YOM_KIPPUR,0,{0,0,0,0,0}},
	{"leif erikson day",DT_HOLIDAY,HD_LEIF_ERIKSON,0,{0,0,0,0,0}},
	{"columbus day",DT_HOLIDAY,HD_COLUMBUS_DAY,0,{0,0,0,0,0}},
	{"mischief night",DT_HOLIDAY,HD_MISCHIEF_NIGHT,0,{0,0,0,0,0}},
	{"halloween",DT_HOLIDAY,HD_HALLOWEEN,0,{0,0,0,0,0}},
	{"all saints day",DT_HOLIDAY,HD_ALL_SAINTS,0,{0,0,0,0,0}},
	{"veterans day",DT_HOLIDAY,HD_VETERANS,0,{0,0,0,0,0}},
	{"verteran's day",DT_HOLIDAY,HD_VETERANS,0,{0,0,0,0,0}},
	{"thanksgiving",DT_HOLIDAY,HD_THANKSGIVING,0,{0,0,0,0,0}},
	{"turkey day",DT_HOLIDAY,HD_THANKSGIVING,0,{0,0,0,0,0}},
	{"black friday",DT_HOLIDAY,HD_BLACK_FRIDAY,0,{0,0,0,0,0}},
	{"pearl harbor day",DT_HOLIDAY,HD_PEARL_HARBOR,0,{0,0,0,0,0}},
	{"pearl harbor rememberance day",DT_HOLIDAY,HD_PEARL_HARBOR,0,{0,0,0,0,0}},
	{"energy conservation day",DT_HOLIDAY,HD_ENERGY_CONS,0,{0,0,0,0,0}},
	{"winter solstice",DT_HOLIDAY,HD_WINTER_SOL,0,{0,0,0,0,0}},
	{"christmas eve",DT_HOLIDAY,HD_CHRISTMAS_EVE,0,{0,0,0,0,0}},
	{"christmas day",DT_HOLIDAY,HD_CHRISTMAS_DAY,0,{0,0,0,0,0}},
	{"christmas",DT_HOLIDAY,HD_CHRISTMAS_DAY,0,{0,0,0,0,0}},
	//{"",DT_HOLIDAY,,0,{0,0,0,0,0}},
	{"new year's eve",DT_HOLIDAY,HD_NEW_YEARS_EVE,0,{0,0,0,0,0}},
	{"new years eve",DT_HOLIDAY,HD_NEW_YEARS_EVE,0,{0,0,0,0,0}},

	// special
	{"holidays",DT_ALL_HOLIDAYS,HD_HOLIDAYS,0,{0,0,0,0,0}},
	{"holiday",DT_ALL_HOLIDAYS,HD_HOLIDAYS,0,{0,0,0,0,0}},

	// special aliases
	{"every day",DT_EVERY_DAY,HD_EVERY_DAY,0,{0,0,0,0,0}},
	{"everyday",DT_EVERY_DAY,HD_EVERY_DAY,0,{0,0,0,0,0}},
	{"every day of the week",DT_EVERY_DAY,HD_EVERY_DAY,0,{0,0,0,0,0}},
	{"seven days a week",DT_EVERY_DAY,HD_EVERY_DAY,0,{0,0,0,0,0}},
	{"7 days a week",DT_EVERY_DAY,HD_EVERY_DAY,0,{0,0,0,0,0}},
	{"seven days per week",DT_EVERY_DAY,HD_EVERY_DAY,0,{0,0,0,0,0}},
	{"7 days per week",DT_EVERY_DAY,HD_EVERY_DAY,0,{0,0,0,0,0}},
	{"daily",DT_EVERY_DAY,HD_EVERY_DAY,0,{0,0,0,0,0}},
	{"nightly",DT_EVERY_DAY,HD_EVERY_DAY,0,{0,0,0,0,0}},
	// weekly chamber music on wednesday and .... at 10am from
	// http://www.trumba.com/calendars/KRQE_Calendar.rss
	{"weekly",DT_EVERY_DAY,HD_EVERY_DAY,0,{0,0,0,0,0}},

	{"weekend",DT_SUBWEEK,HD_WEEKENDS,0,{0,0,0,0,0}},
	{"weekends",DT_SUBWEEK,HD_WEEKENDS,0,{0,0,0,0,0}},
	{"weekday",DT_SUBWEEK,HD_WEEKDAYS,0,{0,0,0,0,0}},
	{"weekdays",DT_SUBWEEK,HD_WEEKDAYS,0,{0,0,0,0,0}},

	// class schedule abbreviations "tth" = tues thurs, mw = mon wed
	{"tth",DT_SUBWEEK,HD_TTH,0,{0,0,0,0,0}},
	{"mw",DT_SUBWEEK,HD_MW,0,{0,0,0,0,0}},
	{"mwf",DT_SUBWEEK,HD_MWF,0,{0,0,0,0,0}},

	{"summer",DT_SEASON,HD_SUMMER,0,{0,0,0,0,0}},
	{"fall",DT_SEASON,HD_FALL,0,{0,0,0,0,0}},
	{"autumn",DT_SEASON,HD_FALL,0,{0,0,0,0,0}},
	{"winter",DT_SEASON,HD_WINTER,0,{0,0,0,0,0}},
	{"spring",DT_SEASON,HD_SPRING,0,{0,0,0,0,0}},
	{"summers",DT_SEASON,HD_SUMMER,0,{0,0,0,0,0}},
	{"autumns",DT_SEASON,HD_FALL,0,{0,0,0,0,0}},
	{"winters",DT_SEASON,HD_WINTER,0,{0,0,0,0,0}},
	{"school year",DT_SEASON,HD_SCHOOL_YEAR,0,{0,0,0,0,0}},
	{"academic year",DT_SEASON,HD_SCHOOL_YEAR,0,{0,0,0,0,0}},
	{"summer semester",DT_SEASON,HD_SUMMER,0,{0,0,0,0,0}},
	{"fall semester",DT_SEASON,HD_FALL,0,{0,0,0,0,0}},
	{"autumn semester",DT_SEASON,HD_FALL,0,{0,0,0,0,0}},
	{"winter semester",DT_SEASON,HD_WINTER,0,{0,0,0,0,0}},
	{"spring semester",DT_SEASON,HD_SPRING,0,{0,0,0,0,0}},
	{"summer semesters",DT_SEASON,HD_SUMMER,0,{0,0,0,0,0}},
	{"fall semesters",DT_SEASON,HD_FALL,0,{0,0,0,0,0}},
	{"autumn semesters",DT_SEASON,HD_FALL,0,{0,0,0,0,0}},
	{"winter semesters",DT_SEASON,HD_WINTER,0,{0,0,0,0,0}},
	{"spring semesters",DT_SEASON,HD_SPRING,0,{0,0,0,0,0}},

	// specialized tods
	{"morning",DT_SUBDAY,HD_MORNING,0,{0,0,0,0,0}},
	{"mornings",DT_SUBDAY,HD_MORNING,0,{0,0,0,0,0}},
	{"afternoon",DT_SUBDAY,HD_AFTERNOON,0,{0,0,0,0,0}},
	{"afternoons",DT_SUBDAY,HD_AFTERNOON,0,{0,0,0,0,0}},
	{"evening",DT_SUBDAY,HD_NIGHT,0,{0,0,0,0,0}},
	{"evenings",DT_SUBDAY,HD_NIGHT,0,{0,0,0,0,0}},
	{"night",DT_SUBDAY,HD_NIGHT,0,{0,0,0,0,0}},
	{"nights",DT_SUBDAY,HD_NIGHT,0,{0,0,0,0,0}},

	// for "1st and 15th of each month" for unm.edu url
	//{"of each month",DT_HOLIDAY,HD_EVERY_MONTH,0,{0,0,0,0,0}},
	//{"of every month",DT_HOLIDAY,HD_EVERY_MONTH,0,{0,0,0,0,0}},
	//{"each month",DT_HOLIDAY,HD_EVERY_MONTH,0,{0,0,0,0,0}},
	//{"every month",DT_HOLIDAY,HD_EVERY_MONTH,0,{0,0,0,0,0}},

     {"last day of the month",DT_SUBMONTH,HD_MONTH_LAST_DAY,0,{0,0,0,0,0}},
     {"last day of every month",DT_SUBMONTH,HD_MONTH_LAST_DAY,0,{0,0,0,0,0}},
     {"last day of each month",DT_SUBMONTH,HD_MONTH_LAST_DAY,0,{0,0,0,0,0}},
     {"first day of the month",DT_SUBMONTH,HD_MONTH_FIRST_DAY,0,{0,0,0,0,0}},
     {"first day of every month",DT_SUBMONTH,HD_MONTH_FIRST_DAY,0,{0,0,0,0,0}},
     {"first day of each month",DT_SUBMONTH,HD_MONTH_FIRST_DAY,0,{0,0,0,0,0}}
};

static HashTableX s_dvt;
static char s_dvbuf[10000];
static bool s_init98 = false;

bool initDateTypes ( ) {
	if ( s_init98 ) return true;
	// only do once
	s_init98 = true;
	// set the keysize to 8 (wid) maps to a dv ptr
	// use 0 for niceness
	if ( !s_dvt.set(8,sizeof(DateVal *),300,s_dvbuf,10000,true,0,"dvts")) 
		return false;
	// mark this
	char localBuf[1000];
	int32_t localBufSize = 1000;
	// load month names and their values into hashtable from above
	for ( int32_t j = 0 ; j<(int32_t)(sizeof(dvs)/sizeof(DateVal));j++){
		// breathe
		//QUICKPOLL(m_niceness);
		// ref it
		DateVal *dv = &dvs[j];
		// set words
		Words tmp; 
		// niceness is 0!
		if ( ! tmp.setxi ( dv->m_str,localBuf,localBufSize,0)){
			char *xx=NULL;*xx=0;}
		// get wids
		int64_t *kwids = tmp.m_wordIds;
		// first word hash is the key
		int64_t h = kwids[0];
		// must be valid
		if ( ! h ) { char *xx=NULL;*xx=0; }
		// reset wid count
		dv->m_numWids = 0;
		// loop over words
		for ( int32_t k = 0 ; k < tmp.m_numWords ;k++ ) {
			// skip if not word alnum
			if ( ! kwids[k] ) 
				continue;
			// sanity
			if ( dv->m_numWids >= MAX_WIDS ) {
				char *xx=NULL;*xx=0; }
			// set initial hash
			dv->m_wids[(int32_t)dv->m_numWids] = kwids[k];
			// inc it
			dv->m_numWids++;
		}
		// get it
		//DateVal *dv = &dvs[j];
		//int32_t      len = gbstrlen(dv->m_str);
		//uint64_t  h   = hash64Lower_utf8(dv->m_str,len);
		// sanity check
		if ( dv->m_val ==  0 ) { char *xx=NULL;*xx=0; }
		if ( dv->m_val < -30 ) { char *xx=NULL;*xx=0; }
		// add should always be success since we are pre-alloc
		if ( ! s_dvt.addKey(&h,&dv)  ){
			char*xx=NULL;*xx=0;}
	}
	return true;
}


bool isMonth ( int64_t wid ) {
	// sanity check
	if ( ! s_init98 ) { char *xx=NULL;*xx=0; }
	// get slot
	int32_t slot = s_dvt.getSlot64 ( &wid );
	// none? no date type then
	if ( slot < 0 ) return false;
	// see if it is a match
	DateVal **dvp = (DateVal **)s_dvt.getValueFromSlot ( slot );
	// get it
	DateVal *dv = *dvp;
	// is it a month?
	return ( dv->m_type == DT_MONTH );
}

// used by Sections::addSentences()
bool isDateType ( int64_t *pwid ) {
	// sanity check
	if ( ! s_init98 ) { initDateTypes(); } // char *xx=NULL;*xx=0; }
	// get slot
	int32_t slot = s_dvt.getSlot64 ( pwid );
	// none? no date type then
	if ( slot < 0 ) return false;
	// see if it is a match
	DateVal **dvp = (DateVal **)s_dvt.getValueFromSlot ( slot );
	// get it
	DateVal *dv = *dvp;
	// get it
	static datetype_t dd = 
		DT_HOLIDAY      | // thanksgiving
		DT_SUBDAY       | // mornings
		DT_SUBWEEK      | // weekends
		DT_SUBMONTH     | // last day of month
		DT_EVERY_DAY    | // 7 days a week
		DT_SEASON       | // summers
		DT_ALL_HOLIDAYS | // "holidays"
		DT_MONTH        |
		DT_DAYNUM       |
		DT_DOW;
	// is it a month?
	return ( dv->m_type & dd);
}


// . get the DT_* type of date this is
// . SUPPORT: "13th day of","12th month",...   "second week"(is a range)
datetype_t Dates::getDateType ( int32_t i , int32_t *val , int32_t *endWord ,
				int64_t *wids , int32_t nw ,
				// does the word "on" preceed word #i?
				bool onPreceeds ) {
	// only init the table once
	//static bool s_init = false;
	// set up the month name hashtable
	if ( ! s_init98 ) initDateTypes();

	// sum for compounds like twenty-first or nineteen hundred and nine
	//int32_t sum = 0;

	// breathe 
	QUICKPOLL ( m_niceness );
	// get slot
	int32_t slot = s_dvt.getSlot64 ( &wids[i] );
	// none? no date type then
	if ( slot < 0 ) return 0;

	// assume ending word is i+1 (i.e. [i,i+1) )
	if ( endWord ) *endWord = i+1;

	// assume no "best match"
	DateVal *best = NULL;

	datetype_t specialTypes = 
		DT_HOLIDAY      | // thanksgiving
		DT_SUBDAY       | // mornings
		DT_SUBWEEK      | // weekends
		DT_SUBMONTH     | // last day of month
		DT_EVERY_DAY    | // 7 days a week
		DT_SEASON       | // summers
		DT_ALL_HOLIDAYS ; // "holidays"

	// loop over matches
	for ( ; slot >= 0 ; slot = s_dvt.getNextSlot ( slot, &wids[i] ) ) {
		// breathe
		QUICKPOLL(m_niceness);
		// see if it is a match
		DateVal **dvp = (DateVal **)s_dvt.getValueFromSlot ( slot );
		// get it
		DateVal *dv = *dvp;
		// return now if not holiday
		if ( ! ( dv->m_type & specialTypes ) ) { //!= DT_HOLIDAY ) {
			// save it
			if ( val ) *val = dv->m_val;
			// and return the type
			return dv->m_type;
		}

		// ignore type holiday, DT_HOLIDAY, for now since it is
		// so ambiguous. people say "Halloween party" or 
		// "x-mas party" but it is never on halloween or christmas.
		// but this loses "closed thanksgivings and christmas"
		/*
		if ( dv->m_type == DT_HOLIDAY &&
		     // news years eve and day are exceptions to this
		     // since timing is so important for them
		     dv->m_val != HD_NEW_YEARS_EVE &&
		     dv->m_val != HD_NEW_YEARS_DAY &&
		     // if the word "on" is before the holiday then assume
		     // it is on that holiday
		     ! onPreceeds ) 
			// otherwise, do not consider the holiday name itself
			// as to when the event is actually occuring
			continue;
		*/

		// if already had a best we need to beat it in # words matched
		if ( best && dv->m_numWids <= best->m_numWids ) continue;

		// . TTh, MW and MWF should be capitalized!!
		// . these are schedule abbreviations used in college
		if ( dv->m_val == HD_TTH ||
		     dv->m_val == HD_MW  ||
		     dv->m_val == HD_MWF ) {
			if ( is_lower_a(m_wptrs[i][0]) )
				return 0;
		}

		// if holdiday is one word, that is a match
		if ( dv->m_numWids == 1 ) {
			// save it as the best match, but could be overriden
			// like how Christmas Eve would override Christmas
			best = dv;
			// try next matching holiday, if any
			continue;
		}
		// limit scan below
		int32_t max = i + 10;
		// limit the limit
		if ( max > nw ) max = nw;
		// the next word
		int32_t next = 1;
		// start right after i
		int32_t j = i + 1;
		// . if holiday has multiple words, we gotta scan!
		// . i.e. "New Year's Eve"
		for ( ; j < max ; j++ ) {
			// all done, all matched
			if ( next >= dv->m_numWids ) break;
			// skip if not word
			if ( ! wids[j] ) continue;
			// if no match, try next slot
			if ( dv->m_wids[next] != wids[j] ) break;
			// match next word now
			next++;
		}
		// if did not match all wids in dv, try next slot
		if ( next < dv->m_numWids ) continue;
		// a new best match
		best = dv;
		// update it
		if ( endWord ) *endWord = j;
	}

	// 0 if no match... maybe just matched first word of a multiword
	// holiday like "New Year's Eve" we matched "New"
	if ( ! best ) return 0;

	// store it
	if ( val ) *val = best->m_val;

	return best->m_type;
}


// month names in various languages
struct Months {
	char month[32];
	char value;
};

Months months[] = {

	// support numbers
	{ "1"  , 1 } ,
	{ "2"  , 2 } ,
	{ "3"  , 3 } ,
	{ "4"  , 4 } ,
	{ "5"  , 5 } ,
	{ "6"  , 6 } ,
	{ "7"  , 7 } ,
	{ "8"  , 8 } ,
	{ "9"  , 9 } ,
	{ "10" ,10 } ,
	{ "11" ,11 } ,
	{ "12" ,12 } ,
	{ "01" , 1 } ,
	{ "02" , 2 } ,
	{ "03" , 3 } ,
	{ "04" , 4 } ,
	{ "05" , 5 } ,
	{ "06" , 6 } ,
	{ "07" , 7 } ,
	{ "08" , 8 } ,
	{ "09" , 9 } 

};

	/*

		// FIRST BY MONTH IN SUPPORTED LANGUAGES
		// JANUARY
		{ "jan"                 , 1 },
		{ "janv"                , 1 },
		{ "janvier"             , 1 },
		{ "januari"             , 1 },
		{ "januar"              , 1 },
		{ "enero"               , 1 },
		// FEBRUARY
		{ "feb"                 , 2 },
		{ "febr"                , 2 },
		{ "februari"            , 2 },
		{ "februar"             , 2 },
		{ "febrero"             , 2 },
		{ "fevr"                , 2 },
		{ "fevrier"             , 2 },
		// MARCH
		{ "mar"                 , 3 },
		{ "mars"                , 3 },
		{ "marzo"               , 3 },
		{ "marec"               , 3 },
		{ "marz"                , 3 },
		{ "maart"               , 3 },
		{ "abr"                 , 3 },
		{ "abril"               , 3 },
		// APRIL
		{ "apr"                 , 4 },
		{ "avril"               , 4 },
		// MAY
		{ "may"                 , 5 },
		{ "mayo"                , 5 },
		{ "mai"                 , 5 },
		{ "mei"                 , 5 },
		{ "maj"                 , 5 },
		// JUNE
		{ "jun"                 , 6 },
		{ "june"                , 6 },
		{ "juni"                , 6 },
		{ "junio"               , 6 },
		{ "junij"               , 6 },
		{ "juin"                , 6 },
		// JULY
		{ "juil"                , 7 },
		{ "juillet"             , 7 },
		{ "jul"                 , 7 },
		{ "july"                , 7 },
		{ "juli"                , 7 },
		{ "julio"               , 7 },
		{ "julij"               , 7 },
		// AUGUST
		{ "aug"                 , 8 },
		{ "august"              , 8 },
		{ "augustus"            , 8 },
		{ "augusti"             , 8 },
		{ "aout"                , 8 },
		{ "agosto"              , 8 },
		{ "avg"                 , 8 },
		{ "avgust"              , 8 },
		// SEPTEMBER
		{ "sep"                 , 9 },
		{ "sept"                , 9 },
		{ "september"           , 9 },
		{ "septembre"           , 9 },
		{ "septiembre"          , 9 },
		{ "set"                 , 9 },
		// OCTOBER
		{ "oct"                 , 10 },
		{ "october"             , 10 },
		{ "octobre"             , 10 },
		{ "octubre"             , 10 },
		{ "okt"                 , 10 },
		{ "oktober"             , 10 },
		// NOVEMBER
		{ "nov"                 , 11 },
		{ "november"            , 11 },
		{ "novembre"            , 11 },
		{ "noviembre"           , 11 },
		// DECEMBER
		{ "dec"                 , 12 },
		{ "december"            , 12 },
		{ "decembre"            , 12 },
		{ "dez"                 , 12 },
		{ "dezember"            , 12 },
		{ "dic"                 , 12 },
		{ "deciembre"           , 12 },
		{ "des"                 , 12 },
		{ "desember"            , 12 },

		// THEN BY LANGUAGE (note: dups are ok)
		// Abaza | абаза (abaza)
		{ "гъынчыльа", 1 },
		{ "январь"      , 1 },
		{ "мазахӀван", 2 },
		{ "февраль"    , 2 },
		{ "гӀапынхъамыз", 3 },
		{ "мартӀ"        , 3 },
		{ "март"          , 3 },
		{ "апрель"      , 4 },
		{ "май"            , 5 },
		{ "пхынхъа"    , 6 },
		{ "июнь"          , 6 },
		{ "пхынчыльа", 7 },
		{ "июль"          , 7 },
		{ "август"      , 8 },
		{ "сентябрь"  , 9 },
		{ "октябрь"    , 10 },
		{ "ноябрь"      , 11 },
		{ "декабрь"    , 12 },
		{ "ġənćəla"       , 1 },
		{ "janvar'"           , 1 },
		{ "mazaḥʷan"       , 2 },
		{ "fevral"            , 2 },
		{ "ʿapənqaməz"     , 3 },
		{ "marṭ"            , 3 },
		{ "mart"              , 3 },
		{ "aprel"             , 4 },
		{ "maj"               , 5 },
		{ "pĥənqa"          , 6 },
		{ "ijun'"             , 6 },
		{ "pĥənćəla"      , 7 },
		{ "ijul"              , 7 },
		{ "avgust"            , 8 },
		{ "sentjabr'"         , 9 },
		{ "oktjabr'"          , 10 },
		{ "nojabr'"           , 11 },
		{ "dekabr'"           , 12 },
		// Abkhaz 1 | аҧсшәа (aṗsšʷa)
		{ "ианвар"      , 1 },
		{ "февраль"    , 2 },
		{ "март"          , 3 },
		{ "апрель"      , 4 },
		{ "маи"            , 5 },
		{ "ииун"          , 6 },
		{ "ииуль"        , 7 },
		{ "август"      , 8 },
		{ "сентиабр"  , 9 },
		{ "октиабр"    , 10 },
		{ "ноиабр"      , 11 },
		{ "декабр"      , 12 },
		{ "ianvar"            , 1 },
		{ "fevral'"           , 2 },
		{ "mart"              , 3 },
		{ "aprel'"            , 4 },
		{ "mai"               , 5 },
		{ "iiun"              , 6 },
		{ "iiul'"             , 7 },
		{ "avgust"            , 8 },
		{ "sentiabr"          , 9 },
		{ "oktiabr"           , 10 },
		{ "noiabr"            , 11 },
		{ "dekabr"            , 12 },
		// Adyghe 1 | адыгэбзэ (adəgăbză) see rus
		{ "мэзае"        , 1 },
		{ "щӀышылэ"    , 2 },
		{ "гъатхэпэ"  , 3 },
		{ "мэлыжьыхь", 4 },
		{ "нэкъыгъэ"  , 5 },
		{ "мэкъуауэгъуэ", 6 },
		{ "бадзэуэгъуэ", 7 },
		{ "шыщхьэӀу"  , 8 },
		{ "фокӀадэ"    , 9 },
		{ "жэпуэгъуэ", 10 },
		{ "щэкӀуэгъуэ", 11 },
		{ "дыгъэгъазэ", 12 },
		{ "măzaje"           , 1 },
		{ "ṣ̌ʿəšəlă"   , 2 },
		{ "ġatĥălă"       , 3 },
		{ "măləẓ̌əḥ"   , 4 },
		{ "năqəġă"        , 5 },
		{ "măqʷauăġʷă"  , 6 },
		{ "badzăuăġʷă"   , 7 },
		{ "šəṣ̌ḥălu"   , 8 },
		{ "foč̤adă"        , 9 },
		{ "žăpuăġʷă"    , 10 },
		{ "ṣ̌ăḳʷăġʷă", 11 },
		{ "dəġăġază"     , 12 },
		// Afrikaans
		{ "januarie"          , 1 },
		{ "februarie"         , 2 },
		{ "maart"             , 3 },
		{ "april"             , 4 },
		{ "mei"               , 5 },
		{ "junie"             , 6 },
		{ "julie"             , 7 },
		{ "augustus"          , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "desember"          , 12 },
		// Alabama | Albaamo innaaɬiilka
		{ "hasi cháffàaka"  , 1 },
		{ "hasi hachàalímmòona hasiholtina aɬɬámmòona", 1 },
		{ "hasiholtina hachàalímmòona", 1 },
		{ "febwiiri"          , 2 },
		{ "hasiholtina istatókla", 2 },
		{ "màchka"           , 3 },
		{ "hasiholtina istatótchìina", 3 },
		{ "eyprilka"          , 4 },
		{ "hasiholtina istonóostàaka", 4 },
		{ "meyka"             , 5 },
		{ "hasiholtina istatáɬɬàapi", 5 },
		{ "hasiholtina istahánnàali", 6 },
		{ "hasiholtina istontóklo", 7 },
		{ "awkoska"           , 8 },
		{ "hasiholtina istontótchìina", 8 },
		{ "hasiholtina istachákkàali", 9 },
		{ "hasiholtina ispókkòoli", 10 },
		{ "hasiholtina istapókkòolawah cháffàaka", 11 },
		{ "hasiholtina istanóoka", 12 },
		{ "hasiholtina istapókkòolawah tóklo", 12 },
		// Albanian | shqip
		{ "janar"             , 1 },
		{ "shkurt"            , 2 },
		{ "mars"              , 3 },
		{ "prill"             , 4 },
		{ "maj"               , 5 },
		{ "qershor"           , 6 },
		{ "korrik"            , 7 },
		{ "gusht"             , 8 },
		{ "shtator"           , 9 },
		{ "tetor"             , 10 },
		{ "nëntor"           , 11 },
		{ "dhjetor"           , 12 },
		// Amharic | አማርኛ (ămarəña) & Tigrinya | ትግርኛ (təgrəña)
		{ "ጃንዩወሪ"   , 1 },
		{ "ፌብሩወሪ"   , 2 },
		{ "ማርች"         , 3 },
		{ "ኤፕረል"      , 4 },
		{ "ሜይ"            , 5 },
		{ "ጁን"            , 6 },
		{ "ጁላይ"         , 7 },
		{ "ኦገስት"      , 8 },
		{ "ሴፕቴምበር", 9 },
		{ "ኦክተውበር", 10 },
		{ "ኖቬምበር"   , 11 },
		{ "ዲሴምበር"   , 12 },
		{ "jañuwări"        , 1 },
		{ "februwări"        , 2 },
		{ "marč"             , 3 },
		{ "epräl"            , 4 },
		{ "mey"               , 5 },
		{ "jun"               , 6 },
		{ "julay"             , 7 },
		{ "ogäst"            , 8 },
		{ "septembär"        , 9 },
		{ "oktäwbär"        , 10 },
		{ "novembär"         , 11 },
		{ "disembär"         , 12 },
		// Arabic 1 | العربية (al-ʿarabīyâ)
		{ "يناير"        , 1 },
		{ "فبراير"      , 2 },
		{ "مارس"          , 3 },
		{ "أبريل"        , 4 },
		{ "مايو"          , 5 },
		{ "يونيو"        , 6 },
		{ "يوليو"        , 7 },
		{ "أغسطس"        , 8 },
		{ "سبتمبر"      , 9 },
		{ "أكتوبر"      , 10 },
		{ "نوفمبر"      , 11 },
		{ "ديسمبر"      , 12 },
		{ "yanāyir"          , 1 },
		{ "fibrāyir"         , 2 },
		{ "māris"            , 3 },
		{ "abrīl"            , 4 },
		{ "māyū"            , 5 },
		{ "yūniyū"          , 6 },
		{ "yūliyū"          , 7 },
		{ "aġusṭus"        , 8 },
		{ "sibtambar"         , 9 },
		{ "uktūbar"          , 10 },
		{ "nūfambar"         , 11 },
		{ "dīsambar"         , 12 },
		// Aragonese | aragonés
		{ "chinero"           , 1 },
		{ "frebero"           , 2 },
		{ "marzo"             , 3 },
		{ "abril"             , 4 },
		{ "mayo"              , 5 },
		{ "chunio"            , 6 },
		{ "chulio"            , 7 },
		{ "agosto"            , 8 },
		{ "setiembre"         , 9 },
		{ "otubre"            , 10 },
		{ "nobiembre"         , 11 },
		{ "abiento"           , 12 },
		// Armenian | Հայերեն (hayeren)
		{ "հունվար"    , 1 },
		{ "փետրվար"    , 2 },
		{ "մարտ"          , 3 },
		{ "ապրիլ"        , 4 },
		{ "մայիս"        , 5 },
		{ "հունիս"      , 6 },
		{ "հուլիս"      , 7 },
		{ "օգոստոս"    , 8 },
		{ "սեպտեմբեր", 9 },
		{ "հոկտեմբեր", 10 },
		{ "նոյեմբեր"  , 11 },
		{ "դեկտեմբեր", 12 },
		{ "hounvar"           , 1 },
		{ "ṗetrvar"         , 2 },
		{ "mart"              , 3 },
		{ "april"             , 4 },
		{ "mayis"             , 5 },
		{ "hounis"            , 6 },
		{ "houlis"            , 7 },
		{ "ōgostos"          , 8 },
		{ "september"         , 9 },
		{ "hoktember"         , 10 },
		{ "noyember"          , 11 },
		{ "dektember"         , 12 },
		// Aromanian | armãneascã
		{ "yinar"             , 1 },
		{ "shcurtu"           , 2 },
		{ "martsu"            , 3 },
		{ "apriir"            , 4 },
		{ "mailu"             , 5 },
		{ "cirisharlu"        , 6 },
		{ "alunarlu"          , 7 },
		{ "avgustu"           , 8 },
		{ "yizmaciunjle"      , 9 },
		{ "xumedru"           , 10 },
		{ "brumarlu"          , 11 },
		{ "andreulu"          , 12 },
		// Assamese | অসমীয়া (ôĥômīyā)
		{ "জানুৱাৰী", 1 },
		{ "ফেব্ৰুৱাৰী", 2 },
		{ "মাৰ্চ"   , 3 },
		{ "এপ্ৰিল", 4 },
		{ "মে"            , 5 },
		{ "জুন"         , 6 },
		{ "জুলাই"   , 7 },
		{ "আগচ্ট"   , 8 },
		{ "চেপ্টেম্বৰ", 9 },
		{ "অক্টোবৰ", 10 },
		{ "নৱেম্বৰ", 11 },
		{ "ডিচেম্বৰ", 12 },
		{ "jānuwārī"       , 1 },
		{ "pʰebruwārī"     , 2 },
		{ "mārč"            , 3 },
		{ "epril"             , 4 },
		{ "me"                , 5 },
		{ "jun"               , 6 },
		{ "julāi"            , 7 },
		{ "āgôčṭ"        , 8 },
		{ "čepṭembôr"     , 9 },
		{ "ôkṭobôr"       , 10 },
		{ "nôwembôr"        , 11 },
		{ "ḍičembôr"      , 12 },
		// Asturian | asturianu
		{ "xineru"            , 1 },
		{ "febreru"           , 2 },
		{ "marzu"             , 3 },
		{ "abril"             , 4 },
		{ "mayu"              , 5 },
		{ "xunu"              , 6 },
		{ "xunetu"            , 7 },
		{ "agostu"            , 8 },
		{ "setiembre"         , 9 },
		{ "ochobre"           , 10 },
		{ "payares"           , 11 },
		{ "avientu"           , 12 },
		// Aymara | aymar
		{ "chichu"            , 1 },
		{ "anata"             , 2 },
		{ "chuqa"             , 3 },
		{ "llamayu"           , 4 },
		{ "qasïwi"           , 5 },
		{ "mara t’aqa"      , 6 },
		{ "huillka kuti"      , 7 },
		{ "llumpaqa"          , 8 },
		{ "sata"              , 9 },
		{ "chika sata"        , 10 },
		{ "lapaka"            , 11 },
		{ "jallu qallta"      , 12 },
		// Azerbaijani | azərbaycanca / азәрбајҹанҹа
		{ "yanvar"            , 1 },
		{ "fevral"            , 2 },
		{ "mart"              , 3 },
		{ "aprel"             , 4 },
		{ "may"               , 5 },
		{ "iyun"              , 6 },
		{ "iyul"              , 7 },
		{ "avqust"            , 8 },
		{ "sentyabr"          , 9 },
		{ "oktyabr"           , 10 },
		{ "noyabr"            , 11 },
		{ "dekabr"            , 12 },
		{ "јанвар"      , 1 },
		{ "феврал"      , 2 },
		{ "март"          , 3 },
		{ "апрел"        , 4 },
		{ "май"            , 5 },
		{ "ијун"          , 6 },
		{ "ијул"          , 7 },
		{ "август"      , 8 },
		{ "сентјабр"  , 9 },
		{ "октјабр"    , 10 },
		{ "нојабр"      , 11 },
		{ "декабр"      , 12 },
		// Bambara | Bamana
		{ "zanwuye"           , 1 },
		{ "zanwiye"           , 1 },
		{ "feburuye"          , 2 },
		{ "marisi"            , 3 },
		{ "awirili"           , 4 },
		{ "mɛ"               , 5 },
		{ "zuɛn"             , 6 },
		{ "zuluye"            , 7 },
		{ "uti"               , 8 },
		{ "sɛtanburu"        , 9 },
		{ "ɔkutɔburu"       , 10 },
		{ "nowanburu"         , 11 },
		{ "desanburu"         , 12 },
		// Bashkir | башҡорт (bašqort)
		{ "ғинуар"      , 1 },
		{ "февраль"    , 2 },
		{ "март"          , 3 },
		{ "апрель"      , 4 },
		{ "май"            , 5 },
		{ "июнь"          , 6 },
		{ "июль"          , 7 },
		{ "август"      , 8 },
		{ "сентябрь"  , 9 },
		{ "октябрь"    , 10 },
		{ "ноябрь"      , 11 },
		{ "декабрь"    , 12 },
		{ "ġinuar"           , 1 },
		{ "fevral'"           , 2 },
		{ "mart"              , 3 },
		{ "aprel'"            , 4 },
		{ "maj"               , 5 },
		{ "ijun'"             , 6 },
		{ "ijul'"             , 7 },
		{ "avgust"            , 8 },
		{ "sentjabr'"         , 9 },
		{ "oktjabr'"          , 10 },
		{ "nojabr'"           , 11 },
		{ "dekabr'"           , 12 },
		// Basque | euskara
		{ "urtarrila"         , 1 },
		{ "otsaila"           , 2 },
		{ "martxoa"           , 3 },
		{ "apirila"           , 4 },
		{ "maiatza"           , 5 },
		{ "ekaina"            , 6 },
		{ "uztaila"           , 7 },
		{ "abuztua"           , 8 },
		{ "iraila"            , 9 },
		{ "urria"             , 10 },
		{ "azaroa"            , 11 },
		{ "abendua"           , 12 },
		// Belarusian | беларуская / biełaruskaja
		{ "студзень"  , 1 },
		{ "люты"          , 2 },
		{ "сакавік"    , 3 },
		{ "красавік"  , 4 },
		{ "май"            , 5 },
		{ "чэрвень"    , 6 },
		{ "ліпень"      , 7 },
		{ "жнівень"    , 8 },
		{ "верасень"  , 9 },
		{ "кастрычнік", 10 },
		{ "лістапад"  , 11 },
		{ "снежань"    , 12 },
		{ "studzień"         , 1 },
		{ "luty"              , 2 },
		{ "sakavik"           , 3 },
		{ "krasavik"          , 4 },
		{ "maj"               , 5 },
		{ "červień"         , 6 },
		{ "lipień"           , 7 },
		{ "žnivień"         , 8 },
		{ "vierasień"        , 9 },
		{ "kastryčnik"       , 10 },
		{ "listapad"          , 11 },
		{ "sniežań"         , 12 },
		// Bengali | বাংলা (bāṁlā)
		{ "জানুয়ারী", 1 },
		{ "ফেব্রুয়ারী", 2 },
		{ "মার্চ"   , 3 },
		{ "এপ্রিল", 4 },
		{ "মে"            , 5 },
		{ "জুন"         , 6 },
		{ "জুলাই"   , 7 },
		{ "আগস্ট"   , 8 },
		{ "সেপ্টেম্বর", 9 },
		{ "অক্টোবর", 10 },
		{ "নভেম্বর", 11 },
		{ "ডিসেম্বর", 12 },
		{ "jānuyārī"       , 1 },
		{ "pʰebruyārī"     , 2 },
		{ "mārč"            , 3 },
		{ "epril"             , 4 },
		{ "me"                , 5 },
		{ "jun"               , 6 },
		{ "julāi"            , 7 },
		{ "āgôsṭ"         , 8 },
		{ "sepṭembôr"      , 9 },
		{ "ôkṭobôr"       , 10 },
		{ "nôbʰembôr"      , 11 },
		{ "ḍisembôr"       , 12 },
		// Bislama
		{ "januware"          , 1 },
		{ "februari"          , 2 },
		{ "maj"               , 3 },
		{ "epril"             , 4 },
		{ "mei"               , 5 },
		{ "jun"               , 6 },
		{ "julae"             , 7 },
		{ "ogis"              , 8 },
		{ "septemba"          , 9 },
		{ "oktoba"            , 10 },
		{ "novemba"           , 11 },
		{ "desemba"           , 12 },
		// Blackfoot | siksiká
		{ "áísstoyiimsstaa" , 1 },
		{ "isspssáísskitsimao’p", 1 },
		{ "ómahksíki’somm", 1 },
		{ "píítaiáí"      , 2 },
		{ "saómmitsiki’somm", 2 },
		{ "sa’aiki’somm"  , 3 },
		{ "matsiyíkkapisaii’somm", 4 },
		{ "aapistsísskitsaato’s", 5 },
		{ "ito’tsisamssootaa", 6 },
		{ "niipiaato’s"     , 6 },
		{ "otsítsipottaatpi pi’kssiiksi", 6 },
		{ "pi’kssííksi otsitaowayiihpiaawa", 6 },
		{ "niipóómahkátoyiiksistsikaa to’s", 7 },
		{ "iitáyiitsimaahkao’p", 8 },
		{ "pákkii’pistsi otsíai’tssp", 8 },
		{ "áwákaasiiki’somm", 9 },
		{ "iitáípa’ksiksini’kayi pa’kki’pistsi", 9 },
		{ "iitáómatapapittssko", 9 },
		{ "mo’kááto’s"  , 10 },
		{ "sa’áiksi itáómatooyi", 10 },
		{ "iitáóhkohtao’p", 11 },
		{ "iitáó’tsstoyi" , 11 },
		{ "iitáóhkanaikokotoyi niítahtaistsi", 11 },
		{ "isstááato’s"   , 12 },
		{ "misámiko’komiaato’s", 12 },
		{ "omahkátoyiiki’sommiatto’s", 12 },
		{ "omahkátoyiiksistsiko", 12 },
		// Bosnian | bosanski / босански
		{ "januar"            , 1 },
		{ "februar"           , 2 },
		{ "mart"              , 3 },
		{ "april"             , 4 },
		{ "maj"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "august"            , 8 },
		{ "septembar"         , 9 },
		{ "oktobar"           , 10 },
		{ "novembar"          , 11 },
		{ "decembar"          , 12 },
		{ "јануар"      , 1 },
		{ "фебруар"    , 2 },
		{ "март"          , 3 },
		{ "април"        , 4 },
		{ "мај"            , 5 },
		{ "јуни"          , 6 },
		{ "јули"          , 7 },
		{ "аугуст"      , 8 },
		{ "септембар", 9 },
		{ "октобар"    , 10 },
		{ "новембар"  , 11 },
		{ "децембар"  , 12 },
		// Breton | brezhoneg
		{ "miz genver"        , 1 },
		{ "miz c’hwevrer"   , 2 },
		{ "miz meurzh"        , 3 },
		{ "miz ebrel"         , 4 },
		{ "miz mae"           , 5 },
		{ "miz mezheven"      , 6 },
		{ "miz gouere"        , 7 },
		{ "miz eost"          , 8 },
		{ "miz gwengolo"      , 9 },
		{ "miz here"          , 10 },
		{ "miz du"            , 11 },
		{ "miz kerzu"         , 12 },
		// Brithenig
		{ "ianeir"            , 1 },
		{ "marth"             , 3 },
		{ "ebril"             , 4 },
		{ "mai"               , 5 },
		{ "methef"            , 6 },
		{ "ffinystiw"         , 7 },
		{ "awst"              , 8 },
		{ "ystreblanc"        , 9 },
		{ "sedref"            , 10 },
		{ "muisnir"           , 11 },
		{ "arbennir"          , 12 },
		// Bulgarian | български (bǎlgarski)
		{ "януари"      , 1 },
		{ "февруари"  , 2 },
		{ "март"          , 3 },
		{ "април"        , 4 },
		{ "май"            , 5 },
		{ "юни"            , 6 },
		{ "юли"            , 7 },
		{ "август"      , 8 },
		{ "септември", 9 },
		{ "октомври"  , 10 },
		{ "ноември"    , 11 },
		{ "декември"  , 12 },
		{ "januari"           , 1 },
		{ "fevruari"          , 2 },
		{ "mart"              , 3 },
		{ "april"             , 4 },
		{ "maj"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "avgust"            , 8 },
		{ "septemvri"         , 9 },
		{ "oktomvri"          , 10 },
		{ "noemvri"           , 11 },
		{ "dekemvri"          , 12 },
		// Burmese | မ္ရန္‌မာစာ (mẏãmasa)
		{ "ဇန္‌နဝာရီ", 1 },
		{ "ဖေဖော္‌ဝာရီ", 2 },
		{ "မတ္"         , 3 },
		{ "ဧပ္ရီ"   , 4 },
		{ "မေ"            , 5 },
		{ "ဇ္ဝန္"   , 6 },
		{ "ဇူလုိင္", 7 },
		{ "ဩဂုတ္"   , 8 },
		{ "စက္‌တင္‌ဘာ", 9 },
		{ "အောက္‌တုိဘာ", 10 },
		{ "နုိဝင္‌ဘာ", 11 },
		{ "ဒီဇင္‌ဘာ", 12 },
		{ "zãnáwaẏi"      , 1 },
		{ "pʰepʰɔwaẏi"   , 2 },
		{ "maʿ"              , 3 },
		{ "epẏi"            , 4 },
		{ "me"                , 5 },
		{ "zũ"               , 6 },
		{ "zulaĩ"            , 7 },
		{ "ɔ̀gouʿ"         , 8 },
		{ "seʿtĩbʰa"       , 9 },
		{ "auʿtobʰa"        , 10 },
		{ "nowĩbʰa"         , 11 },
		{ "dizĩbʰa"         , 12 },
		// Catalan | català
		{ "gener"             , 1 },
		{ "febrer"            , 2 },
		{ "març"             , 3 },
		{ "abril"             , 4 },
		{ "maig"              , 5 },
		{ "juny"              , 6 },
		{ "juliol"            , 7 },
		{ "agost"             , 8 },
		{ "setembre"          , 9 },
		{ "octubre"           , 10 },
		{ "novembre"          , 11 },
		{ "desembre"          , 12 },
		// Chamorro | Chamoru
		{ "ineru"             , 1 },
		{ "fibreru"           , 2 },
		{ "måtso"            , 3 },
		{ "abrit"             , 4 },
		{ "måyu"             , 5 },
		{ "huño"             , 6 },
		{ "hulio"             , 7 },
		{ "agosto"            , 8 },
		{ "septembre"         , 9 },
		{ "oktubri"           , 10 },
		{ "nubembre"          , 11 },
		{ "disembre"          , 12 },
		// Cherokee | ᏣᎳᎩ / tsalagi
		{ "ᏚᏃᎸᏔᏂ"   , 1 },
		{ "ᎧᎦᎵ"         , 2 },
		{ "ᎠᏄᏱ"         , 3 },
		{ "ᎧᏩᏂ"         , 4 },
		{ "ᎠᎾᎠᎬᏘ"   , 5 },
		{ "ᏕᎭᎷᏱ"      , 6 },
		{ "ᎫᏰᏉᏂ"      , 7 },
		{ "ᎦᎶᏂᎢ"      , 8 },
		{ "ᏚᎵᎢᏍᏗ"   , 9 },
		{ "ᏚᏂᏅᏗ"      , 10 },
		{ "ᏄᏓᏕᏆ"      , 11 },
		{ "ᎥᏍᎩᎦ"      , 12 },
		{ "dunolvtani"        , 1 },
		{ "kagali"            , 2 },
		{ "anuyi"             , 3 },
		{ "kawani"            , 4 },
		{ "anaagvti"          , 5 },
		{ "dehaluyi"          , 6 },
		{ "guyequoni"         , 7 },
		{ "galonii"           , 8 },
		{ "duliisdi"          , 9 },
		{ "duninvdi"          , 10 },
		{ "nudadequa"         , 11 },
		{ "vsgiga"            , 12 },
		// Cheyenne | Tsétsêhéstaestse
		{ "hohtseeše’he"   , 1 },
		{ "ma’xêhohtseeše’he", 2 },
		{ "ponoma’a’êhaseneeše’he", 3 },
		{ "vehpotseeše’he" , 4 },
		{ "matse’omeeše’he", 5 },
		{ "enano’eeše’he", 6 },
		{ "meaneeše’he"    , 7 },
		{ "oeneneeše’he"   , 8 },
		{ "tonoeveeše’he"  , 9 },
		{ "se’enehe"        , 10 },
		{ "he’koneneeše’he", 11 },
		{ "ma’xêhe’koneneeše’he", 12 },
		// Chinese | æ¼¢èª/æ±è¯­ (hànyǔ), ᪘ئ (zhōngwén)
		{ "ä¸æ"            , 1 },
		{ "äºæ"            , 2 },
		{ "ä¸æ"            , 3 },
		{ "åæ"            , 4 },
		{ "äºæ"            , 5 },
		{ "å­æ"            , 6 },
		{ "ä¸æ"            , 7 },
		{ "å«æ"            , 8 },
		{ "ä¹æ"            , 9 },
		{ "åæ"            , 10 },
		{ "åä¸æ"         , 11 },
		{ "åäºæ"         , 12 },
		{ "yīyuè"           , 1 },
		{ "èryuè"           , 2 },
		{ "sānyuè"          , 3 },
		{ "sìyuè"           , 4 },
		{ "wǔyuè"           , 5 },
		{ "liùyuè"          , 6 },
		{ "qīyuè"           , 7 },
		{ "bāyuè"           , 8 },
		{ "jiǔyuè"          , 9 },
		{ "shíyuè"          , 10 },
		{ "shíyīyuè"       , 11 },
		{ "shí'èryuè"      , 12 },
		// Chuvash 1 | чӑваш (čăvaš)
		{ "кӑрлач"      , 1 },
		{ "нарӑс"        , 2 },
		{ "кӗҫӗн кӑрлач", 2 },
		{ "пуш"            , 3 },
		{ "ака"            , 4 },
		{ "ҫу"              , 5 },
		{ "ҫӗртме"      , 6 },
		{ "утӑ"            , 7 },
		{ "ҫурла"        , 8 },
		{ "авӑн"          , 9 },
		{ "юпа"            , 10 },
		{ "чӳк"            , 11 },
		{ "раштав"      , 12 },
		{ "kărlač"          , 1 },
		{ "narăs"            , 2 },
		{ "kĕśĕn kărlač" , 2 },
		{ "puš"              , 3 },
		{ "aka"               , 4 },
		{ "śu"               , 5 },
		{ "śĕrtme"          , 6 },
		{ "ută"              , 7 },
		{ "śurla"            , 8 },
		{ "avăn"             , 9 },
		{ "jupa"              , 10 },
		{ "čük"             , 11 },
		{ "raštav"           , 12 },
		// Classical Mongolian | ᠮᠣᠩᠭᠣᠯ (moŋġol)
		{ "ᠨᠢᠭᠡᠳᠦᠭᠡᠷ ᠰᠠᠷ᠎ᠠ", 1 },
		{ "ᠬᠣᠶᠠᠷᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 2 },
		{ "ᠭᠤᠷᠪᠠᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 3 },
		{ "ᠳᠥᠷᠪᠡᠳᠦᠭᠡᠷ ᠰᠠᠷ᠎ᠠ", 4 },
		{ "ᠲᠠᠪᠤᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 5 },
		{ "ᠵᠢᠷᠭᠤᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 6 },
		{ "ᠳᠣᠯᠤᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 7 },
		{ "ᠨᠠᠢᠮᠠᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ᠂ ᠨᠠᠶᠢᠮᠠᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 8 },
		{ "ᠶᠢᠰᠦᠳᠦᠭᠡᠷ ᠰᠠᠷ᠎ᠠ", 9 },
		{ "ᠠᠷᠪᠠᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 10 },
		{ "ᠠᠷᠪᠠᠨ ᠨᠢᠭᠡᠳᠦᠭᠡᠷ ᠰᠠᠷ᠎ᠠ", 11 },
		{ "ᠠᠷᠪᠠᠨ ᠬᠣᠶᠠᠷᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 12 },
		{ "nigedüɣer sar-a" , 1 },
		{ "qoyarduɣar sar-a" , 2 },
		{ "gurbaduɣar sar-a" , 3 },
		{ "dörbedüɣer sar-a", 4 },
		{ "tabuduɣar sar-a"  , 5 },
		{ "jirguduɣar sar-a" , 6 },
		{ "doluduɣar sar-a"  , 7 },
		{ "naimaduɣar sar-a" , 8 },
		{ "nayimaduɣar sar-a", 8 },
		{ "yisüdüɣer sar-a", 9 },
		{ "arbaduɣar sar-a"  , 10 },
		{ "arban nigedüɣer sar-a", 11 },
		{ "arban qoyarduɣar sar-a", 12 },
		// Coptic (Bohairic) | ⲙⲉⲧⲛ̄ⲣⲉⲙⲛ̄ⲭⲏⲙⲓ (metənremənkhīmi)
		{ "Ⲓⲁⲛⲟⲩⲁ̄ⲣⲓⲟⲥ", 1 },
		{ "Ⲫⲉⲃⲣⲟⲩⲁ̄ⲣⲓⲟⲥ", 2 },
		{ "Ⲙⲁⲣⲧⲓⲟⲥ", 3 },
		{ "Ⲁⲡⲣⲓⲗⲓⲟⲥ", 4 },
		{ "Ⲙⲁⲓⲟⲥ"   , 5 },
		{ "Ⲓⲟⲩⲛⲓⲟⲥ", 6 },
		{ "Ⲓⲟⲩⲗⲓⲟⲥ", 7 },
		{ "Ⲁⲩⲅⲟⲩⲥⲧⲟⲥ", 8 },
		{ "Ⲥⲉⲡⲧⲉⲙⲃⲣⲓⲟⲥ", 9 },
		{ "Ⲟⲕⲧⲱⲃⲣⲓⲟⲥ", 10 },
		{ "Ⲛⲟⲉ̄ⲙⲃⲣⲓⲟⲥ", 11 },
		{ "Ⲇⲉⲕⲉⲙⲃⲣⲓⲟⲥ", 12 },
		{ "ianouàrios"       , 1 },
		{ "Ḟevrouàrios"    , 2 },
		{ "martios"           , 3 },
		{ "aprilios"          , 4 },
		{ "maios"             , 5 },
		{ "iounios"           , 6 },
		{ "ioulios"           , 7 },
		{ "augoustos"         , 8 },
		{ "septemvrios"       , 9 },
		{ "oktōvrios"        , 10 },
		{ "noèmvrios"        , 11 },
		{ "dekemvrios"        , 12 },
		// Cornish | Kernewek
		{ "mys genver"        , 1 },
		{ "mys whevrer"       , 2 },
		{ "mys merth"         , 3 },
		{ "mys ebrel"         , 4 },
		{ "mys me"            , 5 },
		{ "mys metheven"      , 6 },
		{ "mys gortheren"     , 7 },
		{ "mys est"           , 8 },
		{ "mys gwyngala"      , 9 },
		{ "mys hedra"         , 10 },
		{ "mys du"            , 11 },
		{ "mys kevardhu"      , 12 },
		// Corsican | corsu
		{ "ghjennaghju"       , 1 },
		{ "ferraghju"         , 2 },
		{ "marzu"             , 3 },
		{ "aprile"            , 4 },
		{ "maghju"            , 5 },
		{ "ghjugnu"           , 6 },
		{ "lugliu"            , 7 },
		{ "aostu"             , 8 },
		{ "sittembre"         , 9 },
		{ "uttobre"           , 10 },
		{ "nuvembre"          , 11 },
		{ "dicembre"          , 12 },
		// Crimean Tatar | qırımtatar / къырымтатар
		{ "ocaq"              , 1 },
		{ "şubat"            , 2 },
		{ "mart"              , 3 },
		{ "nisan"             , 4 },
		{ "mayıs"            , 5 },
		{ "haziran"           , 6 },
		{ "temmuz"            , 7 },
		{ "ağustos"          , 8 },
		{ "eylül"            , 9 },
		{ "ekim"              , 10 },
		{ "qasım"            , 11 },
		{ "aralıq"           , 12 },
		{ "оджакъ"      , 1 },
		{ "шубат"        , 2 },
		{ "март"          , 3 },
		{ "нисан"        , 4 },
		{ "майыс"        , 5 },
		{ "хазиран"    , 6 },
		{ "теммуз"      , 7 },
		{ "агъустос"  , 8 },
		{ "эйлул"        , 9 },
		{ "эким"          , 10 },
		{ "къасым"      , 11 },
		{ "аралыкъ"    , 12 },
		// Croatian | hrvatski
		{ "siječanj"         , 1 },
		{ "veljača"          , 2 },
		{ "ožujak"           , 3 },
		{ "travanj"           , 4 },
		{ "svibanj"           , 5 },
		{ "lipanj"            , 6 },
		{ "srpanj"            , 7 },
		{ "kolovoz"           , 8 },
		{ "rujan"             , 9 },
		{ "listopad"          , 10 },
		{ "studeni"           , 11 },
		{ "prosinac"          , 12 },
		// Czech | čeština
		{ "leden"             , 1 },
		{ "únor"             , 2 },
		{ "březen"           , 3 },
		{ "duben"             , 4 },
		{ "květen"           , 5 },
		{ "červen"           , 6 },
		{ "červenec"         , 7 },
		{ "srpen"             , 8 },
		{ "září"           , 9 },
		{ "říjen"           , 10 },
		{ "listopad"          , 11 },
		{ "prosinec"          , 12 },
		// Danish 1 | dansk
		{ "januar"            , 1 },
		{ "februar"           , 2 },
		{ "marts"             , 3 },
		{ "april"             , 4 },
		{ "maj"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "august"            , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "december"          , 12 },
		// Dari | دری (darī)
		{ "جنوری"        , 1 },
		{ "فبرری"        , 2 },
		{ "مارچ"          , 3 },
		{ "اپریل"        , 4 },
		{ "مئ"              , 5 },
		{ "جون"            , 6 },
		{ "جولای"        , 7 },
		{ "اگست"          , 8 },
		{ "سپتمبر"      , 9 },
		{ "آکتوبر"      , 10 },
		{ "نومبر"        , 11 },
		{ "دسمبر"        , 12 },
		{ "janvarī"          , 1 },
		{ "febrarī"          , 2 },
		{ "mārč"            , 3 },
		{ "aprīl"            , 4 },
		{ "maʾi"             , 5 },
		{ "jūn"              , 6 },
		{ "jūlāy"           , 7 },
		{ "agast"             , 8 },
		{ "septambar"         , 9 },
		{ "āktōbar"         , 10 },
		{ "novambar"          , 11 },
		{ "disambar"          , 12 },
		// Delaware | Lënape
		{ "enikwsi kishux"    , 1 },
		{ "chkwali kishux"    , 2 },
		{ "xamokhwite kishux" , 3 },
		{ "kwetayoxe kishux"  , 4 },
		{ "tainipën"         , 5 },
		{ "kichinipën"       , 6 },
		{ "yakatamwe kishux"  , 7 },
		{ "lainipën"         , 7 },
		{ "sakayoxe kishux"   , 8 },
		{ "winaminge"         , 8 },
		{ "kichitahkok kishux", 9 },
		{ "puksit kishux"     , 10 },
		{ "wini kishux"       , 11 },
		{ "xakhokwe kishux"   , 12 },
		{ "kichiluwàn"       , 12 },
		{ "muxkòtae kishux"  , 12 },
		// Divehi | ދިވެހިބަސް (divehibas)
		{ "ޖެނުއަރީ"  , 1 },
		{ "ފެބުރުއަރީ", 2 },
		{ "މާޗް"          , 3 },
		{ "އެޕްރިލް"  , 4 },
		{ "މޭ"              , 5 },
		{ "ޖޫން"          , 6 },
		{ "ޖުލައި"      , 7 },
		{ "އޯގަސްޓް"  , 8 },
		{ "ސެޕްޓެމްބަރު", 9 },
		{ "އޮކްޓޯބަރު", 10 },
		{ "ނޮވެމްބަރު", 11 },
		{ "ޑިސެމްބަރު", 12 },
		{ "jenu'arī"         , 1 },
		{ "feburu'arī"       , 2 },
		{ "māč"             , 3 },
		{ "epril"             , 4 },
		{ "mē"               , 5 },
		{ "jūn"              , 6 },
		{ "jula'i"            , 7 },
		{ "ōgasṫ"          , 8 },
		{ "sepṫembaru"      , 9 },
		{ "okṫōbaru"       , 10 },
		{ "novembaru"         , 11 },
		{ "ḋisembaru"       , 12 },
		// Dogri | डोगरी (ḍogrī)
		{ "जनवरी"   , 1 },
		{ "फरवरी"   , 2 },
		{ "मार्च"   , 3 },
		{ "अप्रैल", 4 },
		{ "मेई"         , 5 },
		{ "जून"         , 6 },
		{ "जुलाई"   , 7 },
		{ "अगस्त"   , 8 },
		{ "सतम्बर", 9 },
		{ "अक्तूबर", 10 },
		{ "नवम्बर", 11 },
		{ "दसम्बर", 12 },
		{ "janvarī"          , 1 },
		{ "pʰarvarī"        , 2 },
		{ "mārč"            , 3 },
		{ "apræl"            , 4 },
		{ "meī"              , 5 },
		{ "jūn"              , 6 },
		{ "julāī"           , 7 },
		{ "agast"             , 8 },
		{ "satambar"          , 9 },
		{ "aktūbar"          , 10 },
		{ "navambar"          , 11 },
		{ "dasambar"          , 12 },
		// Dutch 1 | Nederlands
		{ "januari"           , 1 },
		{ "februari"          , 2 },
		{ "maart"             , 3 },
		{ "april"             , 4 },
		{ "mei"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "augustus"          , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "december"          , 12 },
		// Dzongkha | རྫོང་ཁ་ (rǳoṅ.kʰa.)
		{ "སྤྱི་ཟླཝ་དང་པོ་", 1 },
		{ "སྤྱི་ཟླཝ་གཉིས་པ་", 2 },
		{ "སྤྱི་ཟླཝ་གསུམ་པ་", 3 },
		{ "སྤྱི་ཟླཝ་བཞི་པ་", 4 },
		{ "སྤྱི་ཟླཝ་ལྔ་པ་", 5 },
		{ "སྤྱི་ཟླཝ་དྲུག་པ་", 6 },
		{ "སྤྱི་ཟླཝ་བདུན་པ་", 7 },
		{ "སྤྱི་ཟླཝ་བརྒྱད་པ་", 8 },
		{ "སྤྱི་ཟླཝ་དགུ་པ་", 9 },
		{ "སྤྱི་ཟླཝ་བཅུ་པ་", 10 },
		{ "སྤྱི་ཟླཝ་བཅུ་གཅིག་པ་", 11 },
		{ "སྤྱི་ཟླཝ་བཅུ་གཉིས་པ་", 12 },
		{ "spyi.zlaw.daṅ.po.", 1 },
		{ "spyi.zlaw.gñis.pa.", 2 },
		{ "spyi.zlaw.gsum.pa.", 3 },
		{ "spyi.zlaw.bži.pa.", 4 },
		{ "spyi.zlaw.lṅa.pa.", 5 },
		{ "spyi.zlaw.drug.pa.", 6 },
		{ "spyi.zlaw.bdun.pa.", 7 },
		{ "spyi.zlaw.brgyad.pa.", 8 },
		{ "spyi.zlaw.dgu.pa." , 9 },
		{ "spyi.zlaw.bču.pa.", 10 },
		{ "spyi.zlaw.bču.gčig.pa.", 11 },
		{ "spyi.zlaw.bču.gñis.pa.", 12 },
		// English
		{ "january"           , 1 },
		{ "february"          , 2 },
		{ "march"             , 3 },
		{ "april"             , 4 },
		{ "may"               , 5 },
		{ "june"              , 6 },
		{ "july"              , 7 },
		{ "august"            , 8 },
		{ "september"         , 9 },
		{ "october"           , 10 },
		{ "november"          , 11 },
		{ "december"          , 12 },
		// Esperanto
		{ "januaro"           , 1 },
		{ "februaro"          , 2 },
		{ "marto"             , 3 },
		{ "aprilo"            , 4 },
		{ "majo"              , 5 },
		{ "junio"             , 6 },
		{ "julio"             , 7 },
		{ "aŭgusto"          , 8 },
		{ "septembro"         , 9 },
		{ "oktobro"           , 10 },
		{ "novembro"          , 11 },
		{ "decembro"          , 12 },
		// Estonian 1 | eesti
		{ "jaanuar"           , 1 },
		{ "veebruar"          , 2 },
		{ "märts"            , 3 },
		{ "aprill"            , 4 },
		{ "mai"               , 5 },
		{ "juuni"             , 6 },
		{ "juuli"             , 7 },
		{ "august"            , 8 },
		{ "september"         , 9 },
		{ "oktoober"          , 10 },
		{ "november"          , 11 },
		{ "detsember"         , 12 },
		// Even 1 | эвэды (ėvėdy) see rus
		{ "тугэни хэе", 1 },
		{ "эври мир"   , 2 },
		{ "эври ечэн" , 3 },
		{ "эври билэн", 4 },
		{ "эври унма" , 5 },
		{ "эври чордакич", 6 },
		{ "дюгани хэе", 7 },
		{ "ойчири чордакич", 8 },
		{ "ойчири унма", 9 },
		{ "ойчири билэн", 10 },
		{ "ойчири ечэн", 11 },
		{ "ойчири мир", 12 },
		{ "tugėni ĥėje"    , 1 },
		{ "ėvri mir"         , 2 },
		{ "ėvri ječėn"     , 3 },
		{ "ėvri bilėn"      , 4 },
		{ "ėvri unma"        , 5 },
		{ "ėvri čordakič"  , 6 },
		{ "djugani ĥėje"    , 7 },
		{ "ojčiri čordakič", 8 },
		{ "ojčiri unma"      , 9 },
		{ "ojčiri bilėn"    , 10 },
		{ "ojčiri ječėn"   , 11 },
		{ "ojčiri mir"       , 12 },
		// Evenki | эвэды (ėvėdy)
		{ "январь"      , 1 },
		{ "мирэ"          , 1 },
		{ "февраль"    , 2 },
		{ "мирэкэн"    , 2 },
		{ "гиравун"    , 2 },
		{ "мирэ"          , 2 },
		{ "март"          , 3 },
		{ "эктэӈкирэ", 3 },
		{ "апрель"      , 4 },
		{ "май"            , 5 },
		{ "июнь"          , 6 },
		{ "мучун"        , 6 },
		{ "июль"          , 7 },
		{ "иркин"        , 7 },
		{ "август"      , 8 },
		{ "иркин"        , 8 },
		{ "сентябрь"  , 9 },
		{ "октябрь"    , 10 },
		{ "ноябрь"      , 11 },
		{ "хугдарпи"  , 11 },
		{ "декабрь"    , 12 },
		{ "хэгдыг"      , 12 },
		{ "janvar'"           , 1 },
		{ "mirė"             , 1 },
		{ "fevral'"           , 2 },
		{ "mirėkėn"         , 2 },
		{ "giravun"           , 2 },
		{ "mirė"             , 2 },
		{ "mart"              , 3 },
		{ "ėktėṅkirė"    , 3 },
		{ "aprel'"            , 4 },
		{ "maj"               , 5 },
		{ "ijun'"             , 6 },
		{ "mučun"            , 6 },
		{ "ijul'"             , 7 },
		{ "irkin"             , 7 },
		{ "avgust"            , 8 },
		{ "irkin"             , 8 },
		{ "sentjabr'"         , 9 },
		{ "oktjabr'"          , 10 },
		{ "nojabr'"           , 11 },
		{ "ĥugdarpi"         , 11 },
		{ "dekabr'"           , 12 },
		{ "ĥėgdyg"          , 12 },
		// Ewe 1 | Ɛʋɛgbɛ
		{ "dzove"             , 1 },
		{ "dzodze"            , 2 },
		{ "tedoxe"            , 3 },
		{ "afɔfiɛ"          , 4 },
		{ "damɛ"             , 5 },
		{ "masa"              , 6 },
		{ "siamlɔm"          , 7 },
		{ "dasiamime"         , 8 },
		{ "anyɔnyɔ"         , 9 },
		{ "kele"              , 10 },
		{ "adeɛmekpɔxe"     , 11 },
		{ "dzome"             , 12 },
		// Faroese | føroyskt
		{ "januar"            , 1 },
		{ "februar"           , 2 },
		{ "mars"              , 3 },
		{ "apríl"            , 4 },
		{ "mai"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "august"            , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "desember"          , 12 },
		// Fijian | vosa Vakaviti
		{ "janueri"           , 1 },
		{ "feperueri"         , 2 },
		{ "maji"              , 3 },
		{ "epereli"           , 4 },
		{ "me"                , 5 },
		{ "june"              , 6 },
		{ "julai"             , 7 },
		{ "okosita"           , 8 },
		{ "sepiteba"          , 9 },
		{ "okotova"           , 10 },
		{ "noveba"            , 11 },
		{ "tiseba"            , 12 },
		// Finnish | suomi
		{ "tammikuu"          , 1 },
		{ "helmikuu"          , 2 },
		{ "maaliskuu"         , 3 },
		{ "huhtikuu"          , 4 },
		{ "toukokuu"          , 5 },
		{ "kesäkuu"          , 6 },
		{ "heinäkuu"         , 7 },
		{ "elokuu"            , 8 },
		{ "syyskuu"           , 9 },
		{ "lokakuu"           , 10 },
		{ "marraskuu"         , 11 },
		{ "joulukuu"          , 12 },
		// Francoprovençal | arpitan
		{ "janviér"          , 1 },
		{ "fevriér"          , 2 },
		{ "mârs"             , 3 },
		{ "avril"             , 4 },
		{ "mê"               , 5 },
		{ "jouen"             , 6 },
		{ "julyèt"           , 7 },
		{ "oût"              , 8 },
		{ "septembro"         , 9 },
		{ "octobro"           , 10 },
		{ "novembro"          , 11 },
		{ "dècembro"         , 12 },
		// French | français
		{ "janvier"           , 1 },
		{ "février"          , 2 },
		{ "mars"              , 3 },
		{ "avril"             , 4 },
		{ "mai"               , 5 },
		{ "juin"              , 6 },
		{ "juillet"           , 7 },
		{ "août"             , 8 },
		{ "septembre"         , 9 },
		{ "octobre"           , 10 },
		{ "novembre"          , 11 },
		{ "décembre"         , 12 },
		// Frisian 1 | Frysk
		{ "jannewaris"        , 1 },
		{ "febrewaris"        , 2 },
		{ "maart"             , 3 },
		{ "april"             , 4 },
		{ "maaie"             , 5 },
		{ "juny"              , 6 },
		{ "july"              , 7 },
		{ "augustus"          , 8 },
		{ "septimber"         , 9 },
		{ "oktober"           , 10 },
		{ "novimber"          , 11 },
		{ "desimber"          , 12 },
		// Friulian | furlan
		{ "genâr"            , 1 },
		{ "fevrâr"           , 2 },
		{ "març"             , 3 },
		{ "avrîl"            , 4 },
		{ "mai"               , 5 },
		{ "jugn"              , 6 },
		{ "lui"               , 7 },
		{ "avost"             , 8 },
		{ "setembar"          , 9 },
		{ "otubar"            , 10 },
		{ "novembar"          , 11 },
		{ "decembar"          , 12 },
		// Gagauz | gagauz / гагауз
		{ "yanvar"            , 1 },
		{ "fevral"            , 2 },
		{ "mart"              , 3 },
		{ "april"             , 4 },
		{ "may"               , 5 },
		{ "iyün"             , 6 },
		{ "iyül"             , 7 },
		{ "avgust"            , 8 },
		{ "sentäbri"         , 9 },
		{ "oktäbri"          , 10 },
		{ "noyabri"           , 11 },
		{ "dekabri"           , 12 },
		{ "январ"        , 1 },
		{ "феврал"      , 2 },
		{ "март"          , 3 },
		{ "април"        , 4 },
		{ "май"            , 5 },
		{ "ийӱн"          , 6 },
		{ "ийӱл"          , 7 },
		{ "август"      , 8 },
		{ "сентӓбри"  , 9 },
		{ "октӓбри"    , 10 },
		{ "ноябри"      , 11 },
		{ "декабри"    , 12 },
		// Gallegan | galego
		{ "xaneiro"           , 1 },
		{ "febreiro"          , 2 },
		{ "marzo"             , 3 },
		{ "abril"             , 4 },
		{ "maio"              , 5 },
		{ "xuño"             , 6 },
		{ "xullo"             , 7 },
		{ "agosto"            , 8 },
		{ "setembro"          , 9 },
		{ "outubro"           , 10 },
		{ "novembro"          , 11 },
		{ "decembro"          , 12 },
		// Georgian | ქართული (ḳarṭuli)
		{ "იანვარი", 1 },
		{ "თებერვალი", 2 },
		{ "მარტი"   , 3 },
		{ "აპრილი", 4 },
		{ "მაისი"   , 5 },
		{ "ივნისი", 6 },
		{ "ივლისი", 7 },
		{ "აგვისტო", 8 },
		{ "სექტემბერი", 9 },
		{ "ოქტომბერი", 10 },
		{ "ნოემბერი", 11 },
		{ "დეკემბერი", 12 },
		{ "ianvari"           , 1 },
		{ "ṭebervali"       , 2 },
		{ "marti"             , 3 },
		{ "aprili"            , 4 },
		{ "maisi"             , 5 },
		{ "ivnisi"            , 6 },
		{ "ivlisi"            , 7 },
		{ "agvisto"           , 8 },
		{ "seḳtemberi"      , 9 },
		{ "oḳtomberi"       , 10 },
		{ "noemberi"          , 11 },
		{ "dekemberi"         , 12 },
		// German 1 | Deutsch / Deutſch
		{ "januar"            , 1 },
		{ "jänner"           , 1 },
		{ "februar"           , 2 },
		{ "feber"             , 2 },
		{ "märz"             , 3 },
		{ "april"             , 4 },
		{ "mai"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "august"            , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "dezember"          , 12 },
		{ "januar"            , 1 },
		{ "jänner"           , 1 },
		{ "februar"           , 2 },
		{ "feber"             , 2 },
		{ "märz"             , 3 },
		{ "april"             , 4 },
		{ "mai"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "auguſt"           , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "dezember"          , 12 },
		// Greek, Katharevousa | ελληνικά, καθαρεύουσα (ellīniká, kaṯareýoysa)
		{ "Ἰανουάριος", 1 },
		{ "Φεβρουάριος", 2 },
		{ "Μάρτιος"    , 3 },
		{ "Ἀπρίλιος" , 4 },
		{ "Μάϊος"        , 5 },
		{ "Ἰούνιος"   , 6 },
		{ "Ἰούλιος"   , 7 },
		{ "Αὔγουστος", 8 },
		{ "Σεπτέμβριος", 9 },
		{ "Ὀκτώβριος", 10 },
		{ "Νοέμβριος", 11 },
		{ "Δεκέμβριος", 12 },
		{ "ianoyários"       , 1 },
		{ "fevroyários"      , 2 },
		{ "mártios"          , 3 },
		{ "aprílios"         , 4 },
		{ "máïos"           , 5 },
		{ "ioýnios"          , 6 },
		{ "ioýlios"          , 7 },
		{ "aúgoystos"        , 8 },
		{ "septémvrios"      , 9 },
		{ "oktṓvrios"       , 10 },
		{ "noémvrios"        , 11 },
		{ "dekémvrios"       , 12 },
		// Greenlandic | kalaallisut
		{ "januari"           , 1 },
		{ "februari"          , 2 },
		{ "martsi"            , 3 },
		{ "aprili"            , 4 },
		{ "maji"              , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "augustusi"         , 8 },
		{ "septemberi"        , 9 },
		{ "oktoberi"          , 10 },
		{ "novemberi"         , 11 },
		{ "decemberi"         , 12 },
		// Gujarati | ગુજરાતી (gujrātī)
		{ "જાન્યુઆરી", 1 },
		{ "ફેબ્રુઆરી", 2 },
		{ "માર્ચ"   , 3 },
		{ "એપ્રિલ", 4 },
		{ "મે"            , 5 },
		{ "જૂન"         , 6 },
		{ "જુલાઈ"   , 7 },
		{ "ઑગસ્ટ"   , 8 },
		{ "સપ્ટેમ્બર", 9 },
		{ "ઑક્ટ્બર", 10 },
		{ "નવેમ્બર", 11 },
		{ "ડિસેમ્બર", 12 },
		{ "jānyuārī"       , 1 },
		{ "pʰebruārī"      , 2 },
		{ "mārč"            , 3 },
		{ "epril"             , 4 },
		{ "me"                , 5 },
		{ "jūn"              , 6 },
		{ "julāī"           , 7 },
		{ "ogasṭ"           , 8 },
		{ "sapṭembar"       , 9 },
		{ "okṭobar"         , 10 },
		{ "navembar"          , 11 },
		{ "ḍisembar"        , 12 },
		// Haitian Creole | krèyol
		{ "janvye"            , 1 },
		{ "fevriye"           , 2 },
		{ "mas"               , 3 },
		{ "avril"             , 4 },
		{ "me"                , 5 },
		{ "jen"               , 6 },
		{ "juyè"             , 7 },
		{ "out"               , 8 },
		{ "septanm"           , 9 },
		{ "oktòb"            , 10 },
		{ "novanm"            , 11 },
		{ "desanm"            , 12 },
		// Hausa | Hausa / حَوْسَ (ḥausa)
		{ "janairu"           , 1 },
		{ "fabrairu"          , 2 },
		{ "fabara’ir"       , 2 },
		{ "maris"             , 3 },
		{ "afril"             , 4 },
		{ "afrilu"            , 4 },
		{ "mayu"              , 5 },
		{ "mayibi"            , 5 },
		{ "yuni"              , 6 },
		{ "yunihi"            , 6 },
		{ "jun"               , 6 },
		{ "yuli"              , 7 },
		{ "yulizi"            , 7 },
		{ "agusta"            , 8 },
		{ "angusta"           , 8 },
		{ "angushat"          , 8 },
		{ "satumba"           , 9 },
		{ "sitamba"           , 9 },
		{ "shatumbar"         , 9 },
		{ "oktoba"            , 10 },
		{ "akatubar"          , 10 },
		{ "nuwamba"           , 11 },
		{ "nuwambar"          , 11 },
		{ "dizamba"           , 12 },
		{ "disamba"           , 12 },
		{ "dijambar"          , 12 },
		{ "dujambar"          , 12 },
		{ "جَنَيْرُ"  , 1 },
		{ "ڢَبْرَيْرُ ؛ ڢَبَرَاءِر", 2 },
		{ "مَارِسْ"    , 3 },
		{ "ٲڢْرِلْ ؛ ٲڢْرِيلُ", 4 },
		{ "مَايُ ؛ مَايِبِ", 5 },
		{ "يُونِ ؛ يُونِحِ ؛ جُنْ", 6 },
		{ "يُولِ ؛ يُولِزِ", 7 },
		{ "ٲغُسْتَ ؛ ٲنْغُسْتَ ؛ ٲنْغُشَتْ", 8 },
		{ "سَتُمْبَ ؛ سِتَمْبَ ؛ شَاتُمْبَرْ", 9 },
		{ "أُكْتوُبَ ؛ ٲكَتُوبَر", 10 },
		{ "نُوَمْبَ ؛ نُوَمْبَرْ", 11 },
		{ "دِيزَمْبَ ؛ دِيسَمْبَ ؛ دِجَمْبَرْ ؛ دُجَمْبَرْ", 12 },
		// Hawaiian | ʻōlelo Hawaiʻi
		{ "ianuali"           , 1 },
		{ "pepeluali"         , 2 },
		{ "malaki"            , 3 },
		{ "ʻapelila"         , 4 },
		{ "mei"               , 5 },
		{ "iune"              , 6 },
		{ "iulai"             , 7 },
		{ "ʻaukake"          , 8 },
		{ "kepakemapa"        , 9 },
		{ "ʻokakopa"         , 10 },
		{ "nowemapa"          , 11 },
		{ "kekemapa"          , 12 },
		// Hebrew | עברית (ʿiṿrît)
		{ "ינואר"        , 1 },
		{ "יאנואר"      , 1 },
		{ "פברואר"      , 2 },
		{ "מרץ"            , 3 },
		{ "מרס"            , 3 },
		{ "מארס"          , 3 },
		{ "אפריל"        , 4 },
		{ "מי"              , 5 },
		{ "מאי"            , 5 },
		{ "יוני"          , 6 },
		{ "יולי"          , 7 },
		{ "אבגוסט"      , 8 },
		{ "אוגוסט"      , 8 },
		{ "ספטמבר"      , 9 },
		{ "אוקטובר"    , 10 },
		{ "נובמבר"      , 11 },
		{ "דצמבר"        , 12 },
		{ "yanûʾar"         , 1 },
		{ "yânûʾar"        , 1 },
		{ "febrûʾar"        , 2 },
		{ "merts"             , 3 },
		{ "mars"              , 3 },
		{ "mârs"             , 3 },
		{ "aprîl"            , 4 },
		{ "may"               , 5 },
		{ "mây"              , 5 },
		{ "yûnî"            , 6 },
		{ "yûlî"            , 7 },
		{ "aṿgûsṭ"       , 8 },
		{ "ôgûsṭ"         , 8 },
		{ "sepṭember"       , 9 },
		{ "ôqṭôber"       , 10 },
		{ "nôṿember"       , 11 },
		{ "detsember"         , 12 },
		// Hindi | हिंदी (hiṁdī)
		{ "जनवरी"   , 1 },
		{ "फ़रवरी", 2 },
		{ "मार्च"   , 3 },
		{ "अप्रैल", 4 },
		{ "मे"            , 5 },
		{ "जून"         , 6 },
		{ "जुलाई"   , 7 },
		{ "अगस्त"   , 8 },
		{ "सितंबर", 9 },
		{ "अकटूबर", 10 },
		{ "नवंबर"   , 11 },
		{ "दिसंबर", 12 },
		{ "janvarī"          , 1 },
		{ "farvarī"          , 2 },
		{ "mārč"            , 3 },
		{ "apræl"            , 4 },
		{ "me"                , 5 },
		{ "jūn"              , 6 },
		{ "julāī"           , 7 },
		{ "agast"             , 8 },
		{ "sitaṁbar"        , 9 },
		{ "aktūbar"          , 10 },
		{ "navaṁbar"        , 11 },
		{ "disaṁbar"        , 12 },
		// Hmong |  / Hmoob
		{ "   /   ", 1 },
		{ "   /   ", 2 },
		{ "   /   ", 3 },
		{ "   /   ", 4 },
		{ "   /   ", 5 },
		{ "   /   ", 6 },
		{ "   /   ", 7 },
		{ "   /   ", 8 },
		{ "   /   ", 9 },
		{ "   /   ", 10 },
		{ "    /    ", 11 },
		{ "    /    ", 12 },
		{ "ib hlis ntuj"      , 1 },
		{ "ob hlis ntuj"      , 2 },
		{ "peb hlis ntuj"     , 3 },
		{ "plaub hlis ntuj"   , 4 },
		{ "tsib hlis ntuj"    , 5 },
		{ "rau hlis ntuj"     , 6 },
		{ "xya hlis ntuj"     , 7 },
		{ "yim hlis ntuj"     , 8 },
		{ "cuaj hlis ntuj"    , 9 },
		{ "kaum hlis ntuj"    , 10 },
		{ "kaum ib hlis ntuj" , 11 },
		{ "kaum ob hlis ntuj" , 12 },
		// Hungarian 1 | magyar
		{ "január"           , 1 },
		{ "február"          , 2 },
		{ "március"          , 3 },
		{ "április"          , 4 },
		{ "május"            , 5 },
		{ "június"           , 6 },
		{ "július"           , 7 },
		{ "augusztus"         , 8 },
		{ "szeptember"        , 9 },
		{ "október"          , 10 },
		{ "november"          , 11 },
		{ "december"          , 12 },
		// Icelandic | íslenska
		{ "janúar"           , 1 },
		{ "febrúar"          , 2 },
		{ "mars"              , 3 },
		{ "apríl"            , 4 },
		{ "maí"              , 5 },
		{ "júní"            , 6 },
		{ "júlí"            , 7 },
		{ "ágúst"           , 8 },
		{ "september"         , 9 },
		{ "október"          , 10 },
		{ "nóvember"         , 11 },
		{ "desember"          , 12 },
		// Ido
		{ "januaro"           , 1 },
		{ "februaro"          , 2 },
		{ "marto"             , 3 },
		{ "aprilo"            , 4 },
		{ "mayo"              , 5 },
		{ "junio"             , 6 },
		{ "julio"             , 7 },
		{ "agosto"            , 8 },
		{ "septembro"         , 9 },
		{ "oktobro"           , 10 },
		{ "novembro"          , 11 },
		{ "decembro"          , 12 },
		// Inari Sami | anarâškielâ
		{ "uđđâivemáánu" , 1 },
		{ "kuovâmáánu"     , 2 },
		{ "njuhčâmáánu"   , 3 },
		{ "cuáŋuimáánu"   , 4 },
		{ "vyesimáánu"      , 5 },
		{ "kesimáánu"       , 6 },
		{ "syeinimáánu"     , 7 },
		{ "porgemáánu"      , 8 },
		{ "čohčâmáánu"   , 9 },
		{ "roovvâdmáánu"   , 10 },
		{ "skammâmáánu"    , 11 },
		{ "juovlâmáánu"    , 12 },
		// Indonesian | bahasa Indonesia / بهاس ايندونيسيا
		{ "januari"           , 1 },
		{ "februari"          , 2 },
		{ "maret"             , 3 },
		{ "april"             , 4 },
		{ "mei"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "agustus"           , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "desember"          , 12 },
		{ "جانواري"    , 1 },
		{ "فيبرواري"  , 2 },
		{ "ماريت"        , 3 },
		{ "اڨريل"        , 4 },
		{ "مي"              , 5 },
		{ "جوني"          , 6 },
		{ "جولي"          , 7 },
		{ "اڬوستوس"    , 8 },
		{ "سيڨتيمبر"  , 9 },
		{ "اوكتوبر"    , 10 },
		{ "نوۏيمبر"    , 11 },
		{ "ديسيمبر"    , 12 },
		// Interlingua
		{ "januario"          , 1 },
		{ "februario"         , 2 },
		{ "martio"            , 3 },
		{ "april"             , 4 },
		{ "maio"              , 5 },
		{ "junio"             , 6 },
		{ "julio"             , 7 },
		{ "augusto"           , 8 },
		{ "septembre"         , 9 },
		{ "october"           , 10 },
		{ "novembre"          , 11 },
		{ "decembre"          , 12 },
		// Inuktitut | ᐃᓄᒃᑎᑐᑦ / inuktitut
		{ "ᔭᓄᐊᕆ"      , 1 },
		{ "ᕕᐳᐊᕆ"      , 2 },
		{ "ᒫᕐᓯ"         , 3 },
		{ "ᐊᐃᐳᕆᓪ"   , 4 },
		{ "ᒪᐃ"            , 5 },
		{ "ᔪᓂ"            , 6 },
		{ "ᔪᓚᐃ"         , 7 },
		{ "ᐊᐅᒍᔅ"      , 8 },
		{ "ᓯᑎᒻᐳᕆ"   , 9 },
		{ "ᐅᒃᑐᐳᕆ"   , 10 },
		{ "ᓄᕕᒻᐳᕆ"   , 11 },
		{ "ᑎᓯᒻᐳᕆ"   , 12 },
		{ "januari"           , 1 },
		{ "vipuari"           , 2 },
		{ "maarsi"            , 3 },
		{ "aipuril"           , 4 },
		{ "mai"               , 5 },
		{ "juni"              , 6 },
		{ "julai"             , 7 },
		{ "augus"             , 8 },
		{ "sitimpuri"         , 9 },
		{ "uktupuri"          , 10 },
		{ "nuvimpuri"         , 11 },
		{ "tisimpuri"         , 12 },
		// Irish Gaelic | Gaeilge / Gaeilge
		{ "eanáir"           , 1 },
		{ "feabhra"           , 2 },
		{ "márta"            , 3 },
		{ "aibreán"          , 4 },
		{ "bealtaine"         , 5 },
		{ "meitheamh"         , 6 },
		{ "iúil"             , 7 },
		{ "lúnasa"           , 8 },
		{ "meán fómhair"    , 9 },
		{ "deireadh fómhair" , 10 },
		{ "samhain"           , 11 },
		{ "nollaig"           , 12 },
		{ "eanáir"           , 1 },
		{ "feaḃra"          , 2 },
		{ "márta"            , 3 },
		{ "aibreán"          , 4 },
		{ "bealtaine"         , 5 },
		{ "meiṫeaṁ"       , 6 },
		{ "iúil"             , 7 },
		{ "lúnasa"           , 8 },
		{ "meán fóṁair"   , 9 },
		{ "deireaḋ fóṁair", 10 },
		{ "saṁain"          , 11 },
		{ "nollaig"           , 12 },
		// Italian | italiano
		{ "gennaio"           , 1 },
		{ "febbraio"          , 2 },
		{ "marzo"             , 3 },
		{ "aprile"            , 4 },
		{ "maggio"            , 5 },
		{ "giugno"            , 6 },
		{ "luglio"            , 7 },
		{ "agosto"            , 8 },
		{ "settembre"         , 9 },
		{ "ottobre"           , 10 },
		{ "novembre"          , 11 },
		{ "dicembre"          , 12 },
		// Japanese 1 | æ¥æ¬èª (nihongo)
		{ "１月"            , 1 },
		{ "２月"            , 2 },
		{ "３月"            , 3 },
		{ "４月"            , 4 },
		{ "５月"            , 5 },
		{ "６月"            , 6 },
		{ "７月"            , 7 },
		{ "８月"            , 8 },
		{ "９月"            , 9 },
		{ "１０月"         , 10 },
		{ "１１月"         , 11 },
		{ "１２月"         , 12 },
		{ "ä¸æ"            , 1 },
		{ "äºæ"            , 2 },
		{ "ä¸æ"            , 3 },
		{ "åæ"            , 4 },
		{ "äºæ"            , 5 },
		{ "å­æ"            , 6 },
		{ "ä¸æ"            , 7 },
		{ "å«æ"            , 8 },
		{ "ä¹æ"            , 9 },
		{ "åæ"            , 10 },
		{ "åä¸æ"         , 11 },
		{ "åäºæ"         , 12 },
		{ "ichigatsu"         , 1 },
		{ "nigatsu"           , 2 },
		{ "sangatsu"          , 3 },
		{ "shigatsu"          , 4 },
		{ "gogatsu"           , 5 },
		{ "rokugatsu"         , 6 },
		{ "shichigatsu"       , 7 },
		{ "hachigatsu"        , 8 },
		{ "kugatsu"           , 9 },
		{ "jūgatsu"          , 10 },
		{ "jūichigatsu"      , 11 },
		{ "jūnigatsu"        , 12 },
		// Javanese | basa Jawa
		{ "januari"           , 1 },
		{ "pébruari"         , 2 },
		{ "maret"             , 3 },
		{ "april"             , 4 },
		{ "mèi"              , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "agustus"           , 8 },
		{ "sèptèmber"       , 9 },
		{ "oktober"           , 10 },
		{ "nopèmber"         , 11 },
		{ "Ḍésèmber"      , 12 },
		// Kalmyk | хальмг (ĥal'mg)
		{ "туула"        , 1 },
		{ "лу"              , 2 },
		{ "моһа"          , 3 },
		{ "мөрн"          , 4 },
		{ "хөн"            , 5 },
		{ "мөчн"          , 6 },
		{ "така"          , 7 },
		{ "ноха"          , 8 },
		{ "һаха"          , 9 },
		{ "хулһн"        , 10 },
		{ "үкр"            , 11 },
		{ "бар"            , 12 },
		{ "tuula"             , 1 },
		{ "lu"                , 2 },
		{ "moġa"             , 3 },
		{ "mörn"             , 4 },
		{ "ĥön"             , 5 },
		{ "möčn"            , 6 },
		{ "taka"              , 7 },
		{ "noĥa"             , 8 },
		{ "ġaĥa"            , 9 },
		{ "ĥulġn"           , 10 },
		{ "ükr"              , 11 },
		{ "bar"               , 12 },
		// Kalé Romani | Romanó Kaló
		{ "enerín"           , 1 },
		{ "ibraín"           , 2 },
		{ "kirdaré"          , 3 },
		{ "alpandy"           , 4 },
		{ "kindalé"          , 5 },
		{ "ňutivé"          , 6 },
		{ "ňuntivé"         , 7 },
		{ "kerosto"           , 8 },
		{ "xetava"            , 9 },
		{ "oktorbar"          , 10 },
		{ "ňudikoy"          , 11 },
		{ "kendebré"         , 12 },
		// Kannada | ಕನ್ನಡ (kannaḍa)
		{ "ಜನವರೀ"   , 1 },
		{ "ಫೆಬ್ರವರೀ", 2 },
		{ "ಮಾರ್ಚ್", 3 },
		{ "ಎಪ್ರಿಲ್", 4 },
		{ "ಮೆ"            , 5 },
		{ "ಜೂನ್"      , 6 },
		{ "ಜುಲೈ"      , 7 },
		{ "ಆಗಸ್ಟ್", 8 },
		{ "ಸಪ್ಟೆಂಬರ್", 9 },
		{ "ಅಕ್ಟೋಬರ್", 10 },
		{ "ನವೆಂಬರ್", 11 },
		{ "ಡಿಸೆಂಬರ್", 12 },
		{ "janvarī"          , 1 },
		{ "pʰebravarī"      , 2 },
		{ "mārč"            , 3 },
		{ "epril"             , 4 },
		{ "me"                , 5 },
		{ "jūn"              , 6 },
		{ "julai"             , 7 },
		{ "āgasṭ"          , 8 },
		{ "sapṭeṁbar"     , 9 },
		{ "akṭōbar"        , 10 },
		{ "naveṁbar"        , 11 },
		{ "ḍiseṁbar"      , 12 },
		// Kapampangan
		{ "eneru"             , 1 },
		{ "pebreru"           , 2 },
		{ "marsu"             , 3 },
		{ "abril"             , 4 },
		{ "mayu"              , 5 },
		{ "juniu"             , 6 },
		{ "juliu"             , 7 },
		{ "agostu"            , 8 },
		{ "septiembri"        , 9 },
		{ "octubri"           , 10 },
		{ "nobiembri"         , 11 },
		{ "disiembri"         , 12 },
		// Karaim | karaj
		{ "artarych aj"       , 1 },
		{ "kural aj"          , 2 },
		{ "baškuschan aj"    , 3 },
		{ "jaz aj"            , 4 },
		{ "ulah aj"           , 5 },
		{ "čirik aj"         , 6 },
		{ "ajrychsy aj"       , 7 },
		{ "kiuź aj"          , 8 },
		{ "sohum aj"          , 9 },
		{ "kyš aj"           , 10 },
		{ "karakyš aj"       , 11 },
		{ "siuviuńč aj"     , 12 },
		// Karakalpak | qaraqalpaq / қарақалпақ
		{ "yanvar"            , 1 },
		{ "fevral"            , 2 },
		{ "mart"              , 3 },
		{ "aprel"             , 4 },
		{ "may"               , 5 },
		{ "iyun"              , 6 },
		{ "iyul"              , 7 },
		{ "avgust"            , 8 },
		{ "sentyabr"          , 9 },
		{ "oktyabr"           , 10 },
		{ "noyabr"            , 11 },
		{ "dekabr"            , 12 },
		{ "январь"      , 1 },
		{ "февраль"    , 2 },
		{ "март"          , 3 },
		{ "апрель"      , 4 },
		{ "май"            , 5 },
		{ "июнь"          , 6 },
		{ "июль"          , 7 },
		{ "август"      , 8 },
		{ "сентябрь"  , 9 },
		{ "октябрь"    , 10 },
		{ "ноябрь"      , 11 },
		{ "декабрь"    , 12 },
		// Kashmiri | کٲشُر (kạ̄šur) / कॉशुर (kọ̄šur)
		{ "جَنْوَرى"  , 1 },
		{ "پھَرْوَرى", 2 },
		{ "مارٕچ"        , 3 },
		{ "اَپْرِل ؛ اَپْريل", 4 },
		{ "مے"              , 5 },
		{ "جٷن"            , 6 },
		{ "جُلَے"        , 7 },
		{ "اَگَسْت"    , 8 },
		{ "سِتَمْبَر", 9 },
		{ "اۆکْتؤبَر", 10 },
		{ "نَوَمْبَر", 11 },
		{ "دِسَمْبَر", 12 },
		{ "जन्वरी", 1 },
		{ "फर्वरी", 2 },
		{ "मारु'च"  , 3 },
		{ "अप्रिल", 4 },
		{ "अप्रेल", 4 },
		{ "मे"            , 5 },
		{ "जून"         , 6 },
		{ "जुलय"      , 7 },
		{ "अगस्त"   , 8 },
		{ "सितम्बर", 9 },
		{ "ओ'क्तोबर", 10 },
		{ "नवम्बर", 11 },
		{ "दिसम्बर", 12 },
		{ "janvarī"          , 1 },
		{ "pʰarvarī"        , 2 },
		{ "mārụč"         , 3 },
		{ "april"             , 4 },
		{ "aprēl"            , 4 },
		{ "mē"               , 5 },
		{ "jūn"              , 6 },
		{ "julay"             , 7 },
		{ "agast"             , 8 },
		{ "sitambar"          , 9 },
		{ "oktōbar"          , 10 },
		{ "navambar"          , 11 },
		{ "disambar"          , 12 },
		// Kashubian | kaszëbsczi
		{ "stëcznik"         , 1 },
		{ "gromicznik"        , 2 },
		{ "strëmiannik"      , 3 },
		{ "łżëkwiat"       , 4 },
		{ "môj"              , 5 },
		{ "czerwińc"         , 6 },
		{ "lëpińc"          , 7 },
		{ "zélnik"           , 8 },
		{ "séwnik"           , 9 },
		{ "rujan"             , 10 },
		{ "lëstopadnik"      , 11 },
		{ "gòdnik"           , 12 },
		// Kazakh 1 | қазақ / qazaq / قازاق
		{ "Қаңтар"      , 1 },
		{ "Ақпан"        , 2 },
		{ "Наурыз"      , 3 },
		{ "Сәуір"        , 4 },
		{ "Көкек"        , 4 },
		{ "Мамыр"        , 5 },
		{ "Маусым"      , 6 },
		{ "Шілде"        , 7 },
		{ "Тамыз"        , 8 },
		{ "Қыркүйек"  , 9 },
		{ "Қазан"        , 10 },
		{ "Қараша"      , 11 },
		{ "Желтоқсан", 12 },
		{ "qañtar"           , 1 },
		{ "aqpan"             , 2 },
		{ "nawrız"           , 3 },
		{ "säwir"            , 4 },
		{ "kökek"            , 4 },
		{ "mamır"            , 5 },
		{ "mawsım"           , 6 },
		{ "Şilde"            , 7 },
		{ "tamız"            , 8 },
		{ "qırküyek"        , 9 },
		{ "qazan"             , 10 },
		{ "qaraşa"           , 11 },
		{ "jeltoqsan"         , 12 },
		{ "قاڭتار"      , 1 },
		{ "اقپان"        , 2 },
		{ "ناۋرىز"      , 3 },
		{ "ءساۋىر ؛ كوكەك", 4 },
		{ "مامىر"        , 5 },
		{ "ماۋسىم"      , 6 },
		{ "شىلدە"        , 7 },
		{ "تامىز"        , 8 },
		{ "قىركۇيەك"  , 9 },
		{ "قازان"        , 10 },
		{ "قاراشا"      , 11 },
		{ "جەلتوقسان", 12 },
		// Khalkha Mongolian | монгол (mongol) / ᠮᠣᠩᠭᠣᠯ (moṅgol)
		{ "1 дүгээр сар", 1 },
		{ "2 дугаар сар", 2 },
		{ "3 дугаар сар", 3 },
		{ "4 дүгээр сар", 4 },
		{ "5 дугаар сар", 5 },
		{ "6 дугаар сар", 6 },
		{ "7 дугаар сар", 7 },
		{ "8 дугаар сар", 8 },
		{ "9 дүгээр сар", 9 },
		{ "10 дугаар сар", 10 },
		{ "11 дүгээр сар", 11 },
		{ "12 дугаар сар", 12 },
		{ "нэгдүгээр сар", 1 },
		{ "хоёрдугаар сар", 2 },
		{ "гуравдугаар сар", 3 },
		{ "дөрөвдүгээр сар", 4 },
		{ "тавдугаар сар", 5 },
		{ "зургадугаар сар", 6 },
		{ "долдугаар сар", 7 },
		{ "наймдугаар сар", 8 },
		{ "есдүгээр сар", 9 },
		{ "аравдугаар сар", 10 },
		{ "арван нэгдүгээр сар", 11 },
		{ "арван хоёрдугаар сар", 12 },
		{ "nägdügäär sar" , 1 },
		{ "ĥoërdugaar sar"  , 2 },
		{ "guravdugaar sar"   , 3 },
		{ "dörövdügäär sar", 4 },
		{ "tavdugaar sar"     , 5 },
		{ "ǳurgadugaar sar"  , 6 },
		{ "doldugaar sar"     , 7 },
		{ "najmdugaar sar"    , 8 },
		{ "jesdügäär sar"  , 9 },
		{ "aravdugaar sar"    , 10 },
		{ "arvan nägdügäär sar", 11 },
		{ "arvan ĥoërdugaar sar", 12 },
		{ "ᠨᠢᠭᠡᠳᠦᠭᠡᠷ ᠰᠠᠷ᠎ᠠ", 1 },
		{ "ᠬᠣᠶᠠᠷᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 2 },
		{ "ᠭᠤᠷᠪᠠᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 3 },
		{ "ᠳᠥᠷᠪᠡᠳᠦᠭᠡᠷ ᠰᠠᠷ᠎ᠠ", 4 },
		{ "ᠲᠠᠪᠤᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 5 },
		{ "ᠵᠢᠷᠭᠤᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 6 },
		{ "ᠳᠣᠯᠤᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 7 },
		{ "ᠨᠠᠢᠮᠠᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 8 },
		{ "ᠶᠢᠰᠦᠳᠦᠭᠡᠷ ᠰᠠᠷ᠎ᠠ", 9 },
		{ "ᠠᠷᠪᠠᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 10 },
		{ "ᠠᠷᠪᠠᠨ ᠨᠢᠭᠡᠳᠦᠭᠡᠷ ᠰᠠᠷ᠎ᠠ", 11 },
		{ "ᠠᠷᠪᠠᠨ ᠬᠣᠶᠠᠷᠳᠤᠭᠠᠷ ᠰᠠᠷ᠎ᠠ", 12 },
		{ "nigedüger sar-a"  , 1 },
		{ "qoyardugar sar-a"  , 2 },
		{ "gurbadugar sar-a"  , 3 },
		{ "dörbedüger sar-a", 4 },
		{ "tabudugar sar-a"   , 5 },
		{ "jirgudugar sar-a"  , 6 },
		{ "doludugar sar-a"   , 7 },
		{ "naimadugar sar-a"  , 8 },
		{ "yisüdüger sar-a" , 9 },
		{ "arbadugar sar-a"   , 10 },
		{ "arban nigedüger sar-a", 11 },
		{ "arban qoyardugar sar-a", 12 },
		// Khmer | ភាសា​ខ្មែរ (pʰāsā kʰmær)
		{ "យ៉ាំងវីយ៉េ", 1 },
		{ "ហ្វេវ្រីយេ", 2 },
		{ "ម៉ាស"      , 3 },
		{ "អាវ្រិល់", 4 },
		{ "ម៉េ"         , 5 },
		{ "យូវ៉ាំង", 6 },
		{ "ស៊ូយេត", 7 },
		{ "អ៊ូត"      , 8 },
		{ "សេតមប្រ", 9 },
		{ "អុកតូប្រ", 10 },
		{ "ណូវ៉មប្រ", 11 },
		{ "ដេសមប្រ", 12 },
		{ "yāṁṅvīye"    , 1 },
		{ "hvevrīye"         , 2 },
		{ "mās"              , 3 },
		{ "āvrĭl"           , 4 },
		{ "me"                , 5 },
		{ "yūvāṁṅ"      , 6 },
		{ "sūyet"            , 7 },
		{ "ūt"               , 8 },
		{ "setamb[r]"         , 9 },
		{ "uktūb[r]"         , 10 },
		{ "ṇūvamb[r]"      , 11 },
		{ "desamb[r]"         , 12 },
		// Klallam | nəxʷsƛ̕áy̕əm̕
		{ "x̣aʔwəsčiʔánəŋ", 1 },
		{ "č̕aʔyéʔəɬ ɬqáy̕əč", 2 },
		{ "x̣áƛ̕ ɬqáy̕əč", 3 },
		{ "čən̕máʔəxʷ" , 4 },
		{ "čən̕lílu"      , 5 },
		{ "čən̕kʷítšən", 6 },
		{ "čən̕q̕ə́čqs", 7 },
		{ "čən̕t̕áqaʔ"  , 8 },
		{ "čən̕hə́nən"  , 9 },
		{ "sx̣ʷúpč"       , 10 },
		{ "čən̕háʔnəŋ" , 11 },
		{ "x̣əp̕sčiʔánəŋ", 12 },
		// Komi 1 | коми (komi)
		{ "январ"        , 1 },
		{ "февраль"    , 2 },
		{ "март"          , 3 },
		{ "апрель"      , 4 },
		{ "май"            , 5 },
		{ "июнь"          , 6 },
		{ "июль"          , 7 },
		{ "август"      , 8 },
		{ "сентябр"    , 9 },
		{ "октябр"      , 10 },
		{ "ноябр"        , 11 },
		{ "декабр"      , 12 },
		{ "janvar"            , 1 },
		{ "fevral'"           , 2 },
		{ "mart"              , 3 },
		{ "aprel'"            , 4 },
		{ "maj"               , 5 },
		{ "ijun'"             , 6 },
		{ "ijul'"             , 7 },
		{ "avgust"            , 8 },
		{ "sentjabr"          , 9 },
		{ "oktjabr"           , 10 },
		{ "nojabr"            , 11 },
		{ "dekabr"            , 12 },
		// Konkani | कोंकणी (koṁkaṇī)
		{ "जानेवारी", 1 },
		{ "फ़ेब्रुवारी", 2 },
		{ "मार्च"   , 3 },
		{ "एप्रिल", 4 },
		{ "मे"            , 5 },
		{ "जून"         , 6 },
		{ "जुलै"      , 7 },
		{ "ओगस्ट"   , 8 },
		{ "सेप्टेंबर", 9 },
		{ "ओक्टोबर", 10 },
		{ "नोव्हेंबर", 11 },
		{ "डिसेंबर", 12 },
		{ "jānevārī"       , 1 },
		{ "februvārī"       , 2 },
		{ "mārč"            , 3 },
		{ "epril"             , 4 },
		{ "me"                , 5 },
		{ "jūn"              , 6 },
		{ "julæ"             , 7 },
		{ "ogasṭ"           , 8 },
		{ "sepṭeṁbar"     , 9 },
		{ "okṭobar"         , 10 },
		{ "novʰeṁbar"      , 11 },
		{ "ḍiseṁbar"      , 12 },
		// Korean | íêµ­ì´ (hangukeo)
		{ "1ì"              , 1 },
		{ "2ì"              , 2 },
		{ "3ì"              , 3 },
		{ "4ì"              , 4 },
		{ "5ì"              , 5 },
		{ "6ì"              , 6 },
		{ "7ì"              , 7 },
		{ "8ì"              , 8 },
		{ "9ì"              , 9 },
		{ "10ì"             , 10 },
		{ "11ì"             , 11 },
		{ "12ì"             , 12 },
		{ "ì¼ì"            , 1 },
		{ "ì´ì"            , 2 },
		{ "ì¼ì"            , 3 },
		{ "ì¬ì"            , 4 },
		{ "ì¤ì"            , 5 },
		{ "ì ì"            , 6 },
		{ "ì¹ ì"            , 7 },
		{ "íì"            , 8 },
		{ "êµ¬ì"            , 9 },
		{ "ìì"            , 10 },
		{ "ì­ì¼ì"         , 11 },
		{ "ì­ì´ì"         , 12 },
		{ "ilweol"            , 1 },
		{ "iweol"             , 2 },
		{ "samweol"           , 3 },
		{ "saweol"            , 4 },
		{ "oweol"             , 5 },
		{ "yuweol"            , 6 },
		{ "chilweol"          , 7 },
		{ "palweol"           , 8 },
		{ "guweol"            , 9 },
		{ "siweol"            , 10 },
		{ "sipilweol"         , 11 },
		{ "sipiweol"          , 12 },
		// Kurdish (Sorani) 1 | کوردی / kurdî, سۆرانی / soranî
		{ "رێبه‌ندان", 1 },
		{ "ره‌شه‌مێ", 2 },
		{ "نه‌ورۆز"   , 3 },
		{ "گولان"        , 4 },
		{ "جۆزه‌ردان", 5 },
		{ "گه‌رماجمان ؛ پووشپه‌ڕ", 6 },
		{ "خه‌رمانان", 7 },
		{ "گه‌لاوێژ" , 8 },
		{ "ڕه‌زبه‌ر", 9 },
		{ "گه‌لاڕێزان", 10 },
		{ "سه‌رماوه‌ز", 11 },
		{ "به‌فرانبار", 12 },
		{ "rêbendan"         , 1 },
		{ "reşemê"          , 2 },
		{ "newroz"            , 3 },
		{ "gulan"             , 4 },
		{ "cozerdan"          , 5 },
		{ "germaciman"        , 6 },
		{ "pûşpeṟ"        , 6 },
		{ "xermanan"          , 7 },
		{ "gelawêj"          , 8 },
		{ "ṟezber"          , 9 },
		{ "gelaṟêzan"      , 10 },
		{ "sermawez"          , 11 },
		{ "befranbar"         , 12 },
		// Kurdish 1 | kurdî / کوردی, kurmancî / کورمانجی
		{ "çile"             , 1 },
		{ "sibat"             , 2 },
		{ "adar"              , 3 },
		{ "nîsan"            , 4 },
		{ "gulan"             , 5 },
		{ "hezîran"          , 6 },
		{ "tîrmeh"           , 7 },
		{ "tebax"             , 8 },
		{ "îlon"             , 9 },
		{ "cotmeh"            , 10 },
		{ "mijdar"            , 11 },
		{ "kanûn"            , 12 },
		{ "چله"            , 1 },
		{ "سبات"          , 2 },
		{ "ئادار"        , 3 },
		{ "نیسان"        , 4 },
		{ "گولان"        , 5 },
		{ "هه‌زیران" , 6 },
		{ "تیرمه‌ه"   , 7 },
		{ "ته‌باخ"     , 8 },
		{ "ئیلۆن"        , 9 },
		{ "جۆتمه‌ه"   , 10 },
		{ "مژدار"        , 11 },
		{ "کانوون"      , 12 },
		// Ladin (Gardena) | ladin de Gherdëina
		{ "jené"             , 1 },
		{ "fauré"            , 2 },
		{ "merz"              , 3 },
		{ "auril"             , 4 },
		{ "mei"               , 5 },
		{ "juni"              , 6 },
		{ "lugio"             , 7 },
		{ "agost"             , 8 },
		{ "setëmber"         , 9 },
		{ "utober"            , 10 },
		{ "nuvëmber"         , 11 },
		{ "dezëmber"         , 12 },
		// Ladino | ג'ודיאו-איספאנייול / djudeo-espanyol
		{ "אינירו"      , 1 },
		{ "פ'יברירו"   , 2 },
		{ "מארסו"        , 3 },
		{ "אב'ריל"       , 4 },
		{ "מאייו"        , 5 },
		{ "ג'וניו"       , 6 },
		{ "ג'וליו"       , 7 },
		{ "אגוסטו"      , 8 },
		{ "סיפטימברי", 9 },
		{ "אוקטוברי"  , 10 },
		{ "נוב'ימברי" , 11 },
		{ "דיסיימברי", 12 },
		{ "enero"             , 1 },
		{ "febrero"           , 2 },
		{ "marso"             , 3 },
		{ "avril"             , 4 },
		{ "mayo"              , 5 },
		{ "djunio"            , 6 },
		{ "djulio"            , 7 },
		{ "agosto"            , 8 },
		{ "septembre"         , 9 },
		{ "oktobre"           , 10 },
		{ "novembre"          , 11 },
		{ "desiembre"         , 12 },
		// Lao 1 | ພາສາ​ລາວ (pʰāsā lāw)
		{ "ມັງກອນ", 1 },
		{ "ກຸມພາ"   , 2 },
		{ "ມີນາ"      , 3 },
		{ "ເມສາ"      , 4 },
		{ "ພຶດສະພາ", 5 },
		{ "ມິຖຸນາ", 6 },
		{ "ກໍລະກົດ", 7 },
		{ "ສິງຫາ"   , 8 },
		{ "ກັນຍາ"   , 9 },
		{ "ຕຸລາ"      , 10 },
		{ "ພະຈິກ"   , 11 },
		{ "ທັນວາ"   , 12 },
		{ "mâṅkɔ̄n"      , 1 },
		{ "kumpʰā"          , 2 },
		{ "mīnā"            , 3 },
		{ "mēsā"            , 4 },
		{ "pʰʉtsapʰā"     , 5 },
		{ "mitʰunā"         , 6 },
		{ "kaṁlakôt"       , 7 },
		{ "siṅhā"          , 8 },
		{ "kânñā"          , 9 },
		{ "tulā"             , 10 },
		{ "pʰačik"          , 11 },
		{ "tʰânvā"         , 12 },
		// Latin | latine
		{ "ianuarius"         , 1 },
		{ "februarius"        , 2 },
		{ "martius"           , 3 },
		{ "aprilis"           , 4 },
		{ "maius"             , 5 },
		{ "iunius"            , 6 },
		{ "iulius"            , 7 },
		{ "augustus"          , 8 },
		{ "september"         , 9 },
		{ "october"           , 10 },
		{ "november"          , 11 },
		{ "december"          , 12 },
		// Latvian | latviešu
		{ "janvāris"         , 1 },
		{ "februāris"        , 2 },
		{ "marts"             , 3 },
		{ "aprīlis"          , 4 },
		{ "maijs"             , 5 },
		{ "jūnijs"           , 6 },
		{ "jūlijs"           , 7 },
		{ "augusts"           , 8 },
		{ "septembris"        , 9 },
		{ "oktobris"          , 10 },
		{ "novembris"         , 11 },
		{ "decembris"         , 12 },
		// Lezgi 1 | лезги (lezgi) see rus
		{ "гьер"          , 1 },
		{ "эхем"          , 2 },
		{ "ибне"          , 3 },
		{ "нава"          , 4 },
		{ "тӀул"          , 5 },
		{ "къамуг"      , 6 },
		{ "чиле"          , 7 },
		{ "пахун"        , 8 },
		{ "мара"          , 9 },
		{ "баскӀум"    , 10 },
		{ "цӀехуьл"    , 11 },
		{ "фандукӀ"    , 12 },
		{ "her"               , 1 },
		{ "ėḫem"           , 2 },
		{ "ibne"              , 3 },
		{ "nava"              , 4 },
		{ "ṭul"             , 5 },
		{ "q̄amug"           , 6 },
		{ "čile"             , 7 },
		{ "paḫun"           , 8 },
		{ "mara"              , 9 },
		{ "basḳum"          , 10 },
		{ "c̣eḫül"        , 11 },
		{ "fanduḳ"          , 12 },
		// Ligurian | líguru
		{ "zenná"            , 1 },
		{ "frevá"            , 2 },
		{ "marsu"             , 3 },
		{ "arví"             , 4 },
		{ "mazzu"             , 5 },
		{ "zûgnu"            , 6 },
		{ "lûggiu"           , 7 },
		{ "agustu"            , 8 },
		{ "settembre"         , 9 },
		{ "ottubre"           , 10 },
		{ "nuvembre"          , 11 },
		{ "dexembre"          , 12 },
		// Limburgish | Limburgs
		{ "jannewarie"        , 1 },
		{ "fibberwarie"       , 2 },
		{ "miert"             , 3 },
		{ "eprèl"            , 4 },
		{ "meij"              , 5 },
		{ "junie"             , 6 },
		{ "julie"             , 7 },
		{ "augustus"          , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "desember"          , 12 },
		// Lingala | lingála
		{ "sánzá ya libosó", 1 },
		{ "sánzá ya míbalé", 2 },
		{ "sánzá ya mísáto", 3 },
		{ "sánzá ya mínéi", 4 },
		{ "sánzá ya mítáno", 5 },
		{ "sánzá ya motóbá", 6 },
		{ "sánzá ya nsambo" , 7 },
		{ "sánzá ya mwambe" , 8 },
		{ "sánzá ya libwa"  , 9 },
		{ "sánzá ya zómí" , 10 },
		{ "sánzá ya zómí na mɔ̌kɔ́", 11 },
		{ "sánzá ya zómí na míbalé", 12 },
		// Lithuanian | lietuvių
		{ "sausis"            , 1 },
		{ "vasaris"           , 2 },
		{ "kovas"             , 3 },
		{ "balandis"          , 4 },
		{ "gegužė"          , 5 },
		{ "birželis"         , 6 },
		{ "liepa"             , 7 },
		{ "rugpjūtis"        , 8 },
		{ "rugsėjis"         , 9 },
		{ "spalis"            , 10 },
		{ "lapkritis"         , 11 },
		{ "gruodis"           , 12 },
		// Livonian | līvõ
		{ "janvār"           , 1 },
		{ "februar"           , 2 },
		{ "märts"            , 3 },
		{ "april"             , 4 },
		{ "maij"              , 5 },
		{ "jūnij"            , 6 },
		{ "jūlij"            , 7 },
		{ "ougust"            , 8 },
		{ "septembõr"        , 9 },
		{ "oktōbõr"         , 10 },
		{ "novembõr"         , 11 },
		{ "detsembõr"        , 12 },
		// Low German 1 | Plattdüütsch / Plattdüütſch
		{ "januoor"           , 1 },
		{ "januwoor"          , 1 },
		{ "februoor"          , 2 },
		{ "feberwoor"         , 2 },
		{ "märz"             , 3 },
		{ "märzmaand"        , 3 },
		{ "april"             , 4 },
		{ "mai"               , 5 },
		{ "maimaand"          , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "aust"              , 8 },
		{ "augst"             , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "dezember"          , 12 },
		{ "januoor"           , 1 },
		{ "januwoor"          , 1 },
		{ "februoor"          , 2 },
		{ "feberwoor"         , 2 },
		{ "märz"             , 3 },
		{ "märzmaand"        , 3 },
		{ "april"             , 4 },
		{ "mai"               , 5 },
		{ "maimaand"          , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "auſt"             , 8 },
		{ "augſt"            , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "dezember"          , 12 },
		// Lower Sorbian | dolnoserbšćina
		{ "januar"            , 1 },
		{ "februar"           , 2 },
		{ "měrc"             , 3 },
		{ "pózymski"         , 3 },
		{ "apryl"             , 4 },
		{ "maj"               , 5 },
		{ "junij"             , 6 },
		{ "julij"             , 7 },
		{ "awgust"            , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "nowember"          , 11 },
		{ "december"          , 12 },
		// Lule Sami | julevsámegiella
		{ "ådåjakmánno"    , 1 },
		{ "guovvamánno"      , 2 },
		{ "sjnjuktjamánno"   , 3 },
		{ "vuoratjismánno"   , 4 },
		{ "moarmesmánno"     , 5 },
		{ "biehtsemánno"     , 6 },
		{ "sjnjilltjamánno"  , 7 },
		{ "bårggemánno"     , 8 },
		{ "ragátmánno"      , 9 },
		{ "gålgådismánno"  , 10 },
		{ "basádismánno"    , 11 },
		{ "javllamánno"      , 12 },
		// Luxembourgish 1 | Lëtzebuergesch / Lëtzebuergeſch
		{ "januar"            , 1 },
		{ "februar"           , 2 },
		{ "mäerz"            , 3 },
		{ "aprëll"           , 4 },
		{ "mee"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "august"            , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "dezember"          , 12 },
		{ "januar"            , 1 },
		{ "februar"           , 2 },
		{ "mäerz"            , 3 },
		{ "aprëll"           , 4 },
		{ "mee"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "auguſt"           , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "dezember"          , 12 },
		// Macedonian | македонски (makedonski)
		{ "јануари"    , 1 },
		{ "февруари"  , 2 },
		{ "март"          , 3 },
		{ "април"        , 4 },
		{ "мај"            , 5 },
		{ "јуни"          , 6 },
		{ "јули"          , 7 },
		{ "август"      , 8 },
		{ "септември", 9 },
		{ "октомври"  , 10 },
		{ "ноември"    , 11 },
		{ "декември"  , 12 },
		{ "januari"           , 1 },
		{ "fevruari"          , 2 },
		{ "mart"              , 3 },
		{ "april"             , 4 },
		{ "maj"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "avgust"            , 8 },
		{ "septemvri"         , 9 },
		{ "oktomvri"          , 10 },
		{ "noemvri"           , 11 },
		{ "dekemvri"          , 12 },
		// Malagasy
		{ "janoary"           , 1 },
		{ "febroary"          , 2 },
		{ "marsa"             , 3 },
		{ "martsa"            , 3 },
		{ "avrily"            , 4 },
		{ "mey"               , 5 },
		{ "may"               , 5 },
		{ "jiona"             , 6 },
		{ "jona"              , 6 },
		{ "jolay"             , 7 },
		{ "aogositra"         , 8 },
		{ "septambra"         , 9 },
		{ "ôktôbra"         , 10 },
		{ "nôvambra"         , 11 },
		{ "desambra"          , 12 },
		// Malay | bahasa Melayu / بهاس ملايو
		{ "januari"           , 1 },
		{ "februari"          , 2 },
		{ "mac"               , 3 },
		{ "april"             , 4 },
		{ "mei"               , 5 },
		{ "jun"               , 6 },
		{ "julai"             , 7 },
		{ "ogos"              , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "disember"          , 12 },
		{ "جانواري"    , 1 },
		{ "فيبرواري"  , 2 },
		{ "مچ"              , 3 },
		{ "اڨريل"        , 4 },
		{ "مي"              , 5 },
		{ "جون"            , 6 },
		{ "جولاي"        , 7 },
		{ "اوڬوس"        , 8 },
		{ "سيڨتيمبر"  , 9 },
		{ "اوكتوبر"    , 10 },
		{ "نوۏيمبر"    , 11 },
		{ "ديسيمبر"    , 12 },
		// Malayalam | മലയാളം (malayāḷaṁ)
		{ "ജനുവരി", 1 },
		{ "ഫെബ്രുവരി", 2 },
		{ "മാര്‍ച്ച്", 3 },
		{ "ഏപ്രില്", 4 },
		{ "മേയ്"      , 5 },
		{ "ജൂണ്"      , 6 },
		{ "ജൂലൈ"      , 7 },
		{ "ആഗസ്റ്റ്", 8 },
		{ "സെപ്റ്റംബര്", 9 },
		{ "ഒക്ടോബര്", 10 },
		{ "നവംബര്", 11 },
		{ "ഡിസംബര്", 12 },
		{ "januvari"          , 1 },
		{ "pʰebruvari"       , 2 },
		{ "mārčč"          , 3 },
		{ "ēpril"            , 4 },
		{ "mēy"              , 5 },
		{ "jūṇ"            , 6 },
		{ "jūlai"            , 7 },
		{ "āgasṟṟ"       , 8 },
		{ "sepṟṟaṁbar"  , 9 },
		{ "okṭōbar"        , 10 },
		{ "navaṁbar"        , 11 },
		{ "ḍisaṁbar"      , 12 },
		// Maltese | Malti
		{ "jannar"            , 1 },
		{ "frar"              , 2 },
		{ "marzu"             , 3 },
		{ "april"             , 4 },
		{ "mejju"             , 5 },
		{ "Ġunju"            , 6 },
		{ "lulju"             , 7 },
		{ "awissu"            , 8 },
		{ "settembru"         , 9 },
		{ "ottubru"           , 10 },
		{ "novembru"          , 11 },
		{ "diċembru"         , 12 },
		// Manipuri / Meitei | mYtYloN / মৈইতৈইলোন (maitailon)
		{ "wacciq"            , 1 },
		{ "pairel"            , 2 },
		{ "lmta"              , 3 },
		{ "sjibu"             , 4 },
		{ "kalen"             , 5 },
		{ "iq"                , 6 },
		{ "iqel"              , 7 },
		{ "twan"              , 8 },
		{ "laqbn"             , 9 },
		{ "mera"              , 10 },
		{ "hiyaqgy"           , 11 },
		{ "poinu"             , 12 },
		{ "wākčiṅ"        , 1 },
		{ "pʰāírel"        , 2 },
		{ "lamtā"            , 3 },
		{ "sajibu"            , 4 },
		{ "kālen"            , 5 },
		{ "íṅa"            , 6 },
		{ "íṅel"           , 7 },
		{ "tʰawān"          , 8 },
		{ "lāṅban"         , 9 },
		{ "merā"             , 10 },
		{ "hiyāṅgai"       , 11 },
		{ "poínu"            , 12 },
		{ "যাকচিঙ", 1 },
		{ "ফাইরেল", 2 },
		{ "লমতা"      , 3 },
		{ "সজিবু"   , 4 },
		{ "কালেন"   , 5 },
		{ "ইঙা"         , 6 },
		{ "ইঙেল"      , 7 },
		{ "থযান"      , 8 },
		{ "লাঙবন"   , 9 },
		{ "মেরা"      , 10 },
		{ "হিয়ঙগৈই", 11 },
		{ "পোইনু"   , 12 },
		{ "wākčiṅ"        , 1 },
		{ "pʰāirel"         , 2 },
		{ "lamtā"            , 3 },
		{ "sajibu"            , 4 },
		{ "kālen"            , 5 },
		{ "iṅā"            , 6 },
		{ "iṅel"            , 7 },
		{ "tʰawān"          , 8 },
		{ "lāṅban"         , 9 },
		{ "merā"             , 10 },
		{ "hiyāṅgai"       , 11 },
		{ "poinu"             , 12 },
		// Manx | Gaelg
		{ "jerrey-geuree"     , 1 },
		{ "toshiaght-arree"   , 2 },
		{ "mayrnt"            , 3 },
		{ "averil"            , 4 },
		{ "boaldyn"           , 5 },
		{ "mean-souree"       , 6 },
		{ "jerrey-souree"     , 7 },
		{ "luanistyn"         , 8 },
		{ "mean-fouyir"       , 9 },
		{ "jerrey-fouyir"     , 10 },
		{ "mee houney"        , 11 },
		{ "mee ny nollick"    , 12 },
		// Maori 1 | reo Māori
		{ "hānuere"          , 1 },
		{ "pepuere"           , 2 },
		{ "maehe"             , 3 },
		{ "Āperira"          , 4 },
		{ "mei"               , 5 },
		{ "hune"              , 6 },
		{ "hūrae"            , 7 },
		{ "Ākuheta"          , 8 },
		{ "hepetema"          , 9 },
		{ "oketopa"           , 10 },
		{ "noema"             , 11 },
		{ "tīhema"           , 12 },
		// Marathi | मराठी (marāṭʰī)
		{ "जानेवारी", 1 },
		{ "फ़ेबृवारी", 2 },
		{ "मार्च"   , 3 },
		{ "एप्रिल", 4 },
		{ "मे"            , 5 },
		{ "जून"         , 6 },
		{ "जुलै"      , 7 },
		{ "ओगस्ट"   , 8 },
		{ "सेप्टेंबर", 9 },
		{ "ओक्टोबर", 10 },
		{ "नोव्हेंबर", 11 },
		{ "डिसेंबर", 12 },
		{ "jānevārī"       , 1 },
		{ "febravārī"       , 2 },
		{ "mārč"            , 3 },
		{ "epril"             , 4 },
		{ "me"                , 5 },
		{ "jūn"              , 6 },
		{ "julæ"             , 7 },
		{ "ogasṭ"           , 8 },
		{ "sepṭeṁbar"     , 9 },
		{ "okṭobar"         , 10 },
		{ "novʰeṁbar"      , 11 },
		{ "ḍiseṁbar"      , 12 },
		// Mauritius Creole | morisyin
		{ "zanvye"            , 1 },
		{ "fevriye"           , 2 },
		{ "mars"              , 3 },
		{ "avril"             , 4 },
		{ "me"                , 5 },
		{ "zin"               , 6 },
		{ "ziyet"             , 7 },
		{ "ut"                , 8 },
		{ "septam"            , 9 },
		{ "oktob"             , 10 },
		{ "novam"             , 11 },
		{ "desam"             , 12 },
		// Meadow Mari 1 | олык марий (olyk marij) see rus
		{ "шорыкйолтылзе", 1 },
		{ "пугыжтылзе", 2 },
		{ "ӱйарнятылзе", 3 },
		{ "вӱдшортылзе", 4 },
		{ "агатылзе"  , 5 },
		{ "пеледыштылзе", 6 },
		{ "сӱремтылзе", 7 },
		{ "сорлатылзе", 8 },
		{ "идымтылзе", 9 },
		{ "шыжатылзе", 10 },
		{ "кылметылзе", 11 },
		{ "телетылзе", 12 },
		{ "šorykjoltylze"    , 1 },
		{ "pugyžtylze"       , 2 },
		{ "üjarnjatylze"     , 3 },
		{ "vüdšortylze"     , 4 },
		{ "agatylze"          , 5 },
		{ "peledyštylze"     , 6 },
		{ "süremtylze"       , 7 },
		{ "sorlatylze"        , 8 },
		{ "idymtylze"         , 9 },
		{ "šyžatylze"       , 10 },
		{ "kylmetylze"        , 11 },
		{ "teletylze"         , 12 },
		// Miami | myaamia
		{ "ayaapia kiilhswa"  , 1 },
		{ "mahkwa kiilhswa"   , 2 },
		{ "mahkoonsa kiilhswa", 3 },
		{ "aanteekwa kiilhswa", 4 },
		{ "cecaahkwa kiilhswa", 5 },
		{ "wiihkoowia kiilhswa", 6 },
		{ "paaphsaahka niipinwiki", 7 },
		{ "kišiinkwia kiilhswa", 8 },
		{ "mihšiiwia kiilhswa", 9 },
		{ "šaašakaayolia kiilhswa", 10 },
		{ "kiiyolia kiilhswa" , 11 },
		{ "ayaapeensa kiilhswa", 12 },
		// Micmac | Mi’gmaq
		{ "pnamujuigu’s"    , 1 },
		{ "apignajit"         , 2 },
		{ "si’gowigu’s"   , 3 },
		{ "penatmuigu’s"    , 4 },
		{ "sqoljuigu’s"     , 5 },
		{ "nipnigu’s"       , 6 },
		{ "ps’guigu’s"    , 7 },
		{ "gisigwegewigu’s" , 8 },
		{ "wigumgewigu’s"   , 9 },
		{ "wigewigu’s"      , 10 },
		{ "gept’gewigu’s" , 11 },
		{ "gesigewigu’s"    , 12 },
		// Moldavian | moldovenească / молдовеняскэ
		{ "ianuarie"          , 1 },
		{ "februarie"         , 2 },
		{ "faur"              , 2 },
		{ "martie"            , 3 },
		{ "aprilie"           , 4 },
		{ "mai"               , 5 },
		{ "iunie"             , 6 },
		{ "iulie"             , 7 },
		{ "august"            , 8 },
		{ "septembrie"        , 9 },
		{ "octombrie"         , 10 },
		{ "noiembrie"         , 11 },
		{ "decembrie"         , 12 },
		{ "януарие"    , 1 },
		{ "фебруарие", 2 },
		{ "фаур"          , 2 },
		{ "мартие"      , 3 },
		{ "априлие"    , 4 },
		{ "май"            , 5 },
		{ "юние"          , 6 },
		{ "юлие"          , 7 },
		{ "аугуст"      , 8 },
		{ "септембрие", 9 },
		{ "октомбрие", 10 },
		{ "ноембрие"  , 11 },
		{ "дечембрие", 12 },
		// Nahuatl | nahuatlahtolli
		{ "tlacenti"          , 1 },
		{ "tlaonti"           , 2 },
		{ "tlayeti"           , 3 },
		{ "tlanauhti"         , 4 },
		{ "tlamacuilti"       , 5 },
		{ "tlachicuazti"      , 6 },
		{ "tlachiconti"       , 7 },
		{ "tlachicueiti"      , 8 },
		{ "tlachicnauhti"     , 9 },
		{ "tlamatlacti"       , 10 },
		{ "tlamactlihuanceti" , 11 },
		{ "tlamactlihuanonti" , 12 },
		// Neapolitan | nnapulitano
		{ "jennaro"           , 1 },
		{ "frevaro"           , 2 },
		{ "màrzo"            , 3 },
		{ "abbrile"           , 4 },
		{ "maggio"            , 5 },
		{ "giùgno"           , 6 },
		{ "luglio"            , 7 },
		{ "aùsto"            , 8 },
		{ "settembre"         , 9 },
		{ "ottovre"           , 10 },
		{ "nuvembre"          , 11 },
		{ "dicembre"          , 12 },
		// Nenets 1 | ненэця” (nenėcjaʿ) see rus
		{ "лимбя ирий", 1 },
		{ "яре ирий"   , 2 },
		{ "сие ниць ирий", 3 },
		{ "ненэй ниць ирий", 4 },
		{ "ты’ саполана ирий", 4 },
		{ "нёвды ирий", 5 },
		{ "неняӈг’ ирий", 6 },
		{ "нявды ирий", 6 },
		{ "пилё ирий" , 7 },
		{ "таӈы ирий" , 7 },
		{ "яв’халы’ ирий", 8 },
		{ "пилю’ ирий", 8 },
		{ "сельбе ирий", 9 },
		{ "вэба ирий" , 9 },
		{ "ӈэрёй ирий", 10 },
		{ "хор’ ирий", 10 },
		{ "носиндалава ирий", 10 },
		{ "нюдя пэвдей", 11 },
		{ "ӈарка пэвдей", 12 },
		{ "limbja irij"       , 1 },
		{ "jare irij"         , 2 },
		{ "sije nic' irij"    , 3 },
		{ "nenėj nic' irij"  , 4 },
		{ "ty’ sapolana irij", 4 },
		{ "nëvdy irij"       , 5 },
		{ "nenjaṅg’ irij" , 6 },
		{ "njavdy irij"       , 6 },
		{ "pilë irij"        , 7 },
		{ "taṅy irij"       , 7 },
		{ "jav’ĥaly’ irij", 8 },
		{ "pilju’ irij"     , 8 },
		{ "sel'be irij"       , 9 },
		{ "vėba irij"        , 9 },
		{ "ṅėrëj irij"    , 10 },
		{ "ĥor’ irij"      , 10 },
		{ "nosindalava irij"  , 10 },
		{ "njudja pėvdej"    , 11 },
		{ "ṅarka pėvdej"   , 12 },
		// Neo
		{ "janar"             , 1 },
		{ "febrar"            , 2 },
		{ "mars"              , 3 },
		{ "april"             , 4 },
		{ "mey"               , 5 },
		{ "yunyo"             , 6 },
		{ "yul"               , 7 },
		{ "agost"             , 8 },
		{ "septem(bro)"       , 9 },
		{ "oktob(bro)"        , 10 },
		{ "novem(bro)"        , 11 },
		{ "decem(bro)"        , 12 },
		// Nepali | नेपाली (nepālī)
		{ "जन्वरी", 1 },
		{ "फेब्रुअरी", 2 },
		{ "मार्च"   , 3 },
		{ "अप्रिल", 4 },
		{ "मई"            , 5 },
		{ "जून"         , 6 },
		{ "जूलाई"   , 7 },
		{ "अगस्त"   , 8 },
		{ "सितेम्बर", 9 },
		{ "अक्टोबर", 10 },
		{ "नोभेम्बर", 11 },
		{ "डिसेम्बर", 12 },
		{ "ǳanvarī"         , 1 },
		{ "pʰebruarī"       , 2 },
		{ "mārc"             , 3 },
		{ "april"             , 4 },
		{ "maī"              , 5 },
		{ "ǳūn"             , 6 },
		{ "ǳūlāī"         , 7 },
		{ "agast"             , 8 },
		{ "sitembar"          , 9 },
		{ "akṭobar"         , 10 },
		{ "nobʰembar"        , 11 },
		{ "ḍisembar"        , 12 },
		// Norman French | nouormand
		{ "jaunvyi"           , 1 },
		{ "févryi"           , 2 },
		{ "mâr"              , 3 },
		{ "avri"              , 4 },
		{ "mouai"             , 5 },
		{ "juin"              , 6 },
		{ "juilet"            , 7 },
		{ "âot"              , 8 },
		{ "s’tembe"         , 9 },
		{ "octobe"            , 10 },
		{ "novembe"           , 11 },
		{ "décembe"          , 12 },
		// Northern Sami | davvisámegiella
		{ "ođđajagemánnu"  , 1 },
		{ "guovvamánnu"      , 2 },
		{ "njukčamánnu"     , 3 },
		{ "cuoŋománnu"      , 4 },
		{ "miessemánnu"      , 5 },
		{ "geassemánnu"      , 6 },
		{ "suoidnemánnu"     , 7 },
		{ "borgemánnu"       , 8 },
		{ "čakčamánnu"     , 9 },
		{ "golggotmánnu"     , 10 },
		{ "skábmamánnu"     , 11 },
		{ "juovlamánnu"      , 12 },
		// Norwegian | norsk
		{ "januar"            , 1 },
		{ "februar"           , 2 },
		{ "mars"              , 3 },
		{ "april"             , 4 },
		{ "mai"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "august"            , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "desember"          , 12 },
		// Novial
		{ "januare"           , 1 },
		{ "februare"          , 2 },
		{ "marte"             , 3 },
		{ "aprile"            , 4 },
		{ "maye"              , 5 },
		{ "june"              , 6 },
		{ "julie"             , 7 },
		{ "auguste"           , 8 },
		{ "septembre"         , 9 },
		{ "oktobre"           , 10 },
		{ "novembre"          , 11 },
		{ "desembre"          , 12 },
		// Occitan | occitan
		{ "genièr"           , 1 },
		{ "febrièr"          , 2 },
		{ "març"             , 3 },
		{ "abrial"            , 4 },
		{ "mai"               , 5 },
		{ "junh"              , 6 },
		{ "julhet"            , 7 },
		{ "agóst"            , 8 },
		{ "setembre"          , 9 },
		{ "octobre"           , 10 },
		{ "novembre"          , 11 },
		{ "decembre"          , 12 },
		// Old Church Slavonic | словѣньскъ (slověnĭskŭ)
		{ "ианѹарии"  , 1 },
		{ "феврѹарии", 2 },
		{ "мартъ"        , 3 },
		{ "априль"      , 4 },
		{ "маи"            , 5 },
		{ "июнии"        , 6 },
		{ "июлии"        , 7 },
		{ "аугѹстъ"    , 8 },
		{ "септѧбрь"  , 9 },
		{ "октобрь"    , 10 },
		{ "ноѩбрь"      , 11 },
		{ "декѧбрь"    , 12 },
		{ "ianuarii"          , 1 },
		{ "fevruarii"         , 2 },
		{ "martŭ"            , 3 },
		{ "aprilĭ"           , 4 },
		{ "mai"               , 5 },
		{ "ijunii"            , 6 },
		{ "ijulii"            , 7 },
		{ "avgustŭ"          , 8 },
		{ "septębrĭ"        , 9 },
		{ "oktobrĭ"          , 10 },
		{ "nojębrĭ"         , 11 },
		{ "dekębrĭ"         , 12 },
		// Old English | Englisc
		{ "se æfterra gēola", 1 },
		{ "solmōnaþ"        , 2 },
		{ "hreþmōnaþ"      , 3 },
		{ "Ēastermōnaþ"    , 4 },
		{ "Þrimilcemōnaþ"  , 5 },
		{ "sēremōnaþ"      , 6 },
		{ "mǣdmōnaþ"       , 7 },
		{ "wēodmōnaþ"      , 8 },
		{ "hāligmōnaþ"     , 9 },
		{ "winterfylleþ"     , 10 },
		{ "blōtmōnaþ"      , 11 },
		{ "gēolmōnaþ"      , 12 },
		// Oriya | ଓଡ଼ିଆ (oṛiā)
		{ "ଜାନୁଆରି", 1 },
		{ "ଫେବ୍ରୁଆରି", 2 },
		{ "ମାର୍ଚ୍ଚ", 3 },
		{ "ଏପ୍ରିଲ", 4 },
		{ "ମେ"            , 5 },
		{ "ଜୁନ"         , 6 },
		{ "ଜୁଲାଇ"   , 7 },
		{ "ଅଗଷ୍ଟ"   , 8 },
		{ "ସେପ୍ଟେମ୍ବର", 9 },
		{ "ଅକ୍ଟୋବର", 10 },
		{ "ନଭେମ୍ବର", 11 },
		{ "ଡିସେମ୍ବର", 12 },
		{ "jānuāri"         , 1 },
		{ "pʰebruāri"       , 2 },
		{ "mārčč"          , 3 },
		{ "epril"             , 4 },
		{ "me"                , 5 },
		{ "jun"               , 6 },
		{ "julāi"            , 7 },
		{ "ôgôṣṭ"       , 8 },
		{ "sepṭembôr"      , 9 },
		{ "ôkṭobôr"       , 10 },
		{ "nôbʰembôr"      , 11 },
		{ "ḍisembôr"       , 12 },
		// Oromo | oromoo
		{ "amajjii"           , 1 },
		{ "guraandhala"       , 2 },
		{ "bitooteessa"       , 3 },
		{ "elba"              , 4 },
		{ "caamsa"            , 5 },
		{ "waxabajjii"        , 6 },
		{ "adooleessa"        , 7 },
		{ "hagayya"           , 8 },
		{ "fuulbana"          , 9 },
		{ "onkololeessa"      , 10 },
		{ "sadaasa"           , 11 },
		{ "muddee"            , 12 },
		// Ossetian | ирон (iron)
		{ "январь"      , 1 },
		{ "февраль"    , 2 },
		{ "март"          , 3 },
		{ "апрель"      , 4 },
		{ "май"            , 5 },
		{ "июнь"          , 6 },
		{ "июль"          , 7 },
		{ "август"      , 8 },
		{ "сентябрь"  , 9 },
		{ "октябрь"    , 10 },
		{ "ноябрь"      , 11 },
		{ "декабрь"    , 12 },
		{ "janvar'"           , 1 },
		{ "fevral'"           , 2 },
		{ "mart"              , 3 },
		{ "aprel'"            , 4 },
		{ "maj"               , 5 },
		{ "ijun'"             , 6 },
		{ "ijul'"             , 7 },
		{ "avgust"            , 8 },
		{ "sentjabr'"         , 9 },
		{ "oktjabr'"          , 10 },
		{ "nojabr'"           , 11 },
		{ "dekabr'"           , 12 },
		// Papiamento | Papiamentu
		{ "yanuari"           , 1 },
		{ "febrüari"         , 2 },
		{ "mart"              , 3 },
		{ "aprel"             , 4 },
		{ "mei"               , 5 },
		{ "yüni"             , 6 },
		{ "yüli"             , 7 },
		{ "ougùstùs"        , 8 },
		{ "sèptèmber"       , 9 },
		{ "òktober"          , 10 },
		{ "novèmber"         , 11 },
		{ "desèmber"         , 12 },
		// Pashto | پښتو (paŝto)
		{ "جنوري"        , 1 },
		{ "فبروري"      , 2 },
		{ "مارچ"          , 3 },
		{ "اپريل"        , 4 },
		{ "مې ؛ مۍ"      , 5 },
		{ "جون"            , 6 },
		{ "جولاي"        , 7 },
		{ "اګست"          , 8 },
		{ "سپتمبر"      , 9 },
		{ "اکتوبر"      , 10 },
		{ "نومبر"        , 11 },
		{ "دسمبر"        , 12 },
		{ "janwarī"          , 1 },
		{ "fabrūarī"        , 2 },
		{ "mārč"            , 3 },
		{ "aprīl"            , 4 },
		{ "me"                , 5 },
		{ "məy"              , 5 },
		{ "jūn"              , 6 },
		{ "jūlāy"           , 7 },
		{ "agəst"            , 8 },
		{ "siptambər"        , 9 },
		{ "aktobər"          , 10 },
		{ "nuwambər"         , 11 },
		{ "disambər"         , 12 },
		// Pedi 1 | sePedi
		{ "janaware"          , 1 },
		{ "feberware"         , 2 },
		{ "matšhe"           , 3 },
		{ "aporele"           , 4 },
		{ "mei"               , 5 },
		{ "june"              , 6 },
		{ "julae"             , 7 },
		{ "agostose"          , 8 },
		{ "setemere"          , 9 },
		{ "oktobore"          , 10 },
		{ "nofemere"          , 11 },
		{ "disemere"          , 12 },
		// Pennsylvania German | Pennsilfaani-Deitsch / Pennſilfaani-Deitſch
		{ "yenner"            , 1 },
		{ "harning"           , 2 },
		{ "marz"              , 3 },
		{ "abril"             , 4 },
		{ "moi"               , 5 },
		{ "tschun"            , 6 },
		{ "tschulei"          , 7 },
		{ "aaguscht"          , 8 },
		{ "augscht"           , 8 },
		{ "september"         , 9 },
		{ "oktower"           , 10 },
		{ "nowember"          , 11 },
		{ "disember"          , 12 },
		{ "dezember"          , 12 },
		{ "yenner"            , 1 },
		{ "harning"           , 2 },
		{ "marz"              , 3 },
		{ "abril"             , 4 },
		{ "moi"               , 5 },
		{ "tſchun"           , 6 },
		{ "tſchulei"         , 7 },
		{ "aaguſcht"         , 8 },
		{ "augſcht"          , 8 },
		{ "september"         , 9 },
		{ "oktower"           , 10 },
		{ "nowember"          , 11 },
		{ "diſember"         , 12 },
		{ "dezember"          , 12 },
		// Persian 1 | فارسی (fārsī)
		{ "ژانویه"      , 1 },
		{ "فوریه"        , 2 },
		{ "مارس"          , 3 },
		{ "آوریل"        , 4 },
		{ "مه"              , 5 },
		{ "ژوئن"          , 6 },
		{ "ژوئیه ؛ ژویه", 7 },
		{ "اوت"            , 8 },
		{ "سپتامبر"    , 9 },
		{ "اکتبر"        , 10 },
		{ "نوامبر"      , 11 },
		{ "دسامبر"      , 12 },
		{ "žanvīye"         , 1 },
		{ "fevrīye"          , 2 },
		{ "mārs"             , 3 },
		{ "āvrīl"           , 4 },
		{ "me"                , 5 },
		{ "žūʾan"          , 6 },
		{ "žūʾīye"        , 7 },
		{ "žūye"            , 7 },
		{ "ūt"               , 8 },
		{ "septāmbr"         , 9 },
		{ "oktobr"            , 10 },
		{ "novāmbr"          , 11 },
		{ "desāmbr"          , 12 },
		// Plautdietsch / Plautdietſch
		{ "jaunwoa"           , 1 },
		{ "febawoa"           , 2 },
		{ "moaz"              , 3 },
		{ "aprel"             , 4 },
		{ "mai"               , 5 },
		{ "jüni"             , 6 },
		{ "jüli"             , 7 },
		{ "august"            , 8 },
		{ "septamba"          , 9 },
		{ "oktoba"            , 10 },
		{ "novamba"           , 11 },
		{ "dezamba"           , 12 },
		{ "jaunwoa"           , 1 },
		{ "febawoa"           , 2 },
		{ "moaz"              , 3 },
		{ "aprel"             , 4 },
		{ "mai"               , 5 },
		{ "jüni"             , 6 },
		{ "jüli"             , 7 },
		{ "auguſt"           , 8 },
		{ "septamba"          , 9 },
		{ "oktoba"            , 10 },
		{ "novamba"           , 11 },
		{ "dezamba"           , 12 },
		// Polish | polski
		{ "styczeń"          , 1 },
		{ "luty"              , 2 },
		{ "marzec"            , 3 },
		{ "kwiecień"         , 4 },
		{ "maj"               , 5 },
		{ "czerwiec"          , 6 },
		{ "lipiec"            , 7 },
		{ "sierpień"         , 8 },
		{ "wrzesień"         , 9 },
		{ "październik"      , 10 },
		{ "listopad"          , 11 },
		{ "grudzień"         , 12 },
		// Portuguese | português
		{ "janeiro"           , 1 },
		{ "fevereiro"         , 2 },
		{ "março"            , 3 },
		{ "abril"             , 4 },
		{ "maio"              , 5 },
		{ "junho"             , 6 },
		{ "julho"             , 7 },
		{ "agosto"            , 8 },
		{ "setembro"          , 9 },
		{ "outubro"           , 10 },
		{ "novembro"          , 11 },
		{ "dezembro"          , 12 },
		// Provençal | prouvençau
		{ "janvié"           , 1 },
		{ "febrié"           , 2 },
		{ "mars"              , 3 },
		{ "abriéu"           , 4 },
		{ "mai"               , 5 },
		{ "jun"               , 6 },
		{ "juliet"            , 7 },
		{ "avoust"            , 8 },
		{ "sètembre"         , 9 },
		{ "óutobre"          , 10 },
		{ "nouvèmbre"        , 11 },
		{ "desèmbre"         , 12 },
		// Punjabi (India) | ਪੰਜਾਬੀ (paṁjābī)
		{ "ਜਨਵਰੀ"   , 1 },
		{ "ਫ਼ਰਵਰੀ"   , 2 },
		{ "ਮਾਰਚ"      , 3 },
		{ "ਅਪ੍ਰੈਲ", 4 },
		{ "ਮਈ"            , 5 },
		{ "ਜੂਨ"         , 6 },
		{ "ਜੁਲਾਈ"   , 7 },
		{ "ਅਗਸਤ"      , 8 },
		{ "ਸਤੰਬਰ"   , 9 },
		{ "ਅਕਤੂਬਰ", 10 },
		{ "ਨਵੰਬਰ"   , 11 },
		{ "ਦਸੰਬਰ"   , 12 },
		{ "janvarī"          , 1 },
		{ "farvarī"          , 2 },
		{ "mārč"            , 3 },
		{ "apræl"            , 4 },
		{ "maī"              , 5 },
		{ "jūn"              , 6 },
		{ "julāī"           , 7 },
		{ "agast"             , 8 },
		{ "sataṁbar"        , 9 },
		{ "aktūbar"          , 10 },
		{ "navaṁbar"        , 11 },
		{ "dasaṁbar"        , 12 },
		// Quechua 1 | Runasimi; Qhichwa
		{ "qhulla puquy killa", 1 },
		{ "hatun puquy killa" , 2 },
		{ "pawqar waray killa", 3 },
		{ "ayriway killa"     , 4 },
		{ "aymuray killa"     , 5 },
		{ "inti raymi killa"  , 6 },
		{ "anta situwa killa" , 7 },
		{ "chakra yapuy killa", 8 },
		{ "qhapaq situwa killa", 8 },
		{ "tarpuy killa"      , 9 },
		{ "quya raymi killa"  , 9 },
		{ "uma raymi killa"   , 9 },
		{ "kantaray killa"    , 10 },
		{ "ayamarq’a killa" , 11 },
		{ "qhapaq raymi killa", 12 },
		// Raeto-Romance, Grisons | rumantsch grischun
		{ "schaner"           , 1 },
		{ "favrer"            , 2 },
		{ "mars"              , 3 },
		{ "avrigl"            , 4 },
		{ "matg"              , 5 },
		{ "zercladur"         , 6 },
		{ "fanadur"           , 7 },
		{ "avust"             , 8 },
		{ "settember"         , 9 },
		{ "october"           , 10 },
		{ "november"          , 11 },
		{ "december"          , 12 },
		// Red Kurdish 1 (Caucasus) | kurdî / к’ӧрди / کوردی, kurmancî / кӧрманщи / کورمانجی
		{ "kanûna sanî"     , 1 },
		{ "kanûna paşin"    , 1 },
		{ "şivat"            , 2 },
		{ "sibat"             , 2 },
		{ "adar"              , 3 },
		{ "nîsan"            , 4 },
		{ "gulan"             , 5 },
		{ "ḧezîran"        , 6 },
		{ "tîrmeh"           , 7 },
		{ "tîrme"            , 7 },
		{ "temûz"            , 7 },
		{ "tebax"             , 8 },
		{ "îlon"             , 9 },
		{ "îlûn"            , 9 },
		{ "teşrînê ewil"   , 10 },
		{ "teşrînê pêşin", 10 },
		{ "çirîya ewil"     , 10 },
		{ "çirîya pêşin"  , 10 },
		{ "teşrînê sanî"  , 11 },
		{ "teşrînê paşin" , 11 },
		{ "çirîya sanî"    , 11 },
		{ "çirîyapaşin"    , 11 },
		{ "kanûna ewil"      , 12 },
		{ "kanûna pêşin"   , 12 },
		{ "к’ануна сани", 1 },
		{ "к’ануна пашьн", 1 },
		{ "шьват"        , 2 },
		{ "сьбат"        , 2 },
		{ "адар"          , 3 },
		{ "нисан"        , 4 },
		{ "гӧлан"        , 5 },
		{ "һ’әзиран" , 6 },
		{ "тирмәһ"      , 7 },
		{ "тирмә"        , 7 },
		{ "т’әмуз"     , 7 },
		{ "т’әбах"     , 8 },
		{ "илон"          , 9 },
		{ "илун"          , 9 },
		{ "т’әшрине әwьл", 10 },
		{ "т’әшрине пешьн", 10 },
		{ "чьрийа әwьл", 10 },
		{ "чьрийа пешьн", 10 },
		{ "т’әшрине сани", 11 },
		{ "т’әшрине пашьн", 11 },
		{ "чьрийа сани", 11 },
		{ "чьрийа пашьн", 11 },
		{ "к’ануна әwьл", 12 },
		{ "к’ануна пешьн", 12 },
		{ "کانوونا سانی ؛ کانوونا پاشن", 1 },
		{ "شڤات ؛ سبات", 2 },
		{ "ئادار"        , 3 },
		{ "نیسان"        , 4 },
		{ "گولان"        , 5 },
		{ "حه‌زیران" , 6 },
		{ "تیرمه‌ه ؛ تیرمه‌ ؛ ته‌مووز", 7 },
		{ "ته‌باخ"     , 8 },
		{ "ئیلۆن ؛ ئیلوون", 9 },
		{ "ته‌شرینێ ئه‌ول ؛ ته‌شرینێ پێشن ؛ چرییا ئه‌ول ؛ چرییا پێشن", 10 },
		{ "ته‌شرینێ سانی ؛ ته‌شرینێ پاشن ؛ چرییا سانی ؛ چرییا پاشن", 11 },
		{ "کانوونا ئه‌ول ؛ کانوونا پێشن", 12 },
		// Romanian | română
		{ "ianuarie"          , 1 },
		{ "februarie"         , 2 },
		{ "martie"            , 3 },
		{ "aprilie"           , 4 },
		{ "mai"               , 5 },
		{ "iunie"             , 6 },
		{ "iulie"             , 7 },
		{ "august"            , 8 },
		{ "septembrie"        , 9 },
		{ "octombrie"         , 10 },
		{ "noiembrie"         , 11 },
		{ "decembrie"         , 12 },
		// Rundi | kiRundi
		{ "ukwambwere"        , 1 },
		{ "ukwakabiri"        , 2 },
		{ "ukwagatatu"        , 3 },
		{ "ukwakane"          , 4 },
		{ "ukwagatanu"        , 5 },
		{ "ukwagatandatu"     , 6 },
		{ "ukwindwi"          , 7 },
		{ "ukwumunani"        , 8 },
		{ "ukwicenda"         , 9 },
		{ "ukwicumi"          , 10 },
		{ "ukwicuminarimwe"   , 11 },
		{ "ukwicuminakabiri"  , 12 },
		// Russian | русский (russkij)
		{ "январь"      , 1 },
		{ "февраль"    , 2 },
		{ "март"          , 3 },
		{ "апрель"      , 4 },
		{ "май"            , 5 },
		{ "июнь"          , 6 },
		{ "июль"          , 7 },
		{ "август"      , 8 },
		{ "сентябрь"  , 9 },
		{ "октябрь"    , 10 },
		{ "ноябрь"      , 11 },
		{ "декабрь"    , 12 },
		{ "janvar'"           , 1 },
		{ "fevral'"           , 2 },
		{ "mart"              , 3 },
		{ "aprel'"            , 4 },
		{ "maj"               , 5 },
		{ "ijun'"             , 6 },
		{ "ijul'"             , 7 },
		{ "avgust"            , 8 },
		{ "sentjabr'"         , 9 },
		{ "oktjabr'"          , 10 },
		{ "nojabr'"           , 11 },
		{ "dekabr'"           , 12 },
		// Saanich | xʷsenəčqən
		{ "siʔsət"          , 1 },
		{ "ŋiʔŋənəʔ"    , 2 },
		{ "wəx̣əs"         , 3 },
		{ "pəx̣sisəŋ"     , 4 },
		{ "sx̣ʷeʔnəɬ"    , 5 },
		{ "pən̕exʷəŋ"    , 6 },
		{ "čən̕θəqəy̕" , 7 },
		{ "čən̕hənən"    , 8 },
		{ "čən̕θew̕ən"  , 9 },
		{ "pəq̕əlenəxʷ"  , 10 },
		{ "xʷəsəlenəxʷ"  , 11 },
		{ "xʷsčəl̕kʷeʔsən", 12 },
		// Saint Lucia Creole | kwéyòl
		{ "janvyé"           , 1 },
		{ "févwiyé"         , 2 },
		{ "mas"               , 3 },
		{ "avwi"              , 4 },
		{ "mé"               , 5 },
		{ "jen"               , 6 },
		{ "jwiyèt"           , 7 },
		{ "au"                , 8 },
		{ "sèptanm"          , 9 },
		{ "òktòb"           , 10 },
		{ "novanm"            , 11 },
		{ "désanm"           , 12 },
		// Sakha / Yakut | саха (saĥa)
		{ "тохсунньу", 1 },
		{ "олунньу"    , 2 },
		{ "кулун тутар ый", 3 },
		{ "муус устар ый", 4 },
		{ "ыам ыйа"     , 5 },
		{ "бэс ыйа"     , 6 },
		{ "от ыйа"       , 7 },
		{ "атырдьах ыйа", 8 },
		{ "балаҕан ыйа", 9 },
		{ "алтынньы"  , 10 },
		{ "сэтинньи"  , 11 },
		{ "ахсынньы"  , 12 },
		{ "toĥsunn'u"        , 1 },
		{ "olunn'u"           , 2 },
		{ "kulun tutar yj"    , 3 },
		{ "muus ustar yj"     , 4 },
		{ "yam yja"           , 5 },
		{ "bäs yja"          , 6 },
		{ "ot yja"            , 7 },
		{ "atyrd'aĥ yja"     , 8 },
		{ "balaġan yja"      , 9 },
		{ "altynn'y"          , 10 },
		{ "sätinn'i"         , 11 },
		{ "aĥsynn'y"         , 12 },
		// Sango 1 | sängö
		{ "zamviëe"          , 1 },
		{ "fevriëe"          , 2 },
		{ "mârsi"            , 3 },
		{ "avrîli"           , 4 },
		{ "mêe"              , 5 },
		{ "zuyën"            , 6 },
		{ "zuyêti"           , 7 },
		{ "ûti"              , 8 },
		{ "sëtâmbere"       , 9 },
		{ "ötôbere"         , 10 },
		{ "növâmbere"       , 11 },
		{ "dïsâmbere"       , 12 },
		// Sanskrit | संस्कृतम् (saṁskr̥tam)
		{ "जनवरी"   , 1 },
		{ "फरवरी"   , 2 },
		{ "मार्च"   , 3 },
		{ "अप्रैल", 4 },
		{ "मई"            , 5 },
		{ "जून"         , 6 },
		{ "जुलाई"   , 7 },
		{ "अगस्त"   , 8 },
		{ "सितम्बर", 9 },
		{ "अक्तूबर", 10 },
		{ "नवम्बर", 11 },
		{ "दिसम्बर", 12 },
		{ "janvarī"          , 1 },
		{ "pʰarvarī"        , 2 },
		{ "mārč"            , 3 },
		{ "apræl"            , 4 },
		{ "maī"              , 5 },
		{ "jūn"              , 6 },
		{ "julāī"           , 7 },
		{ "agast"             , 8 },
		{ "sitambar"          , 9 },
		{ "aktūbar"          , 10 },
		{ "navambar"          , 11 },
		{ "disambar"          , 12 },
		// Sardinian | sardu
		{ "bennàlzu"         , 1 },
		{ "fiàrgiu"          , 2 },
		{ "màltu"            , 3 },
		{ "abríbi"           , 4 },
		{ "màgiu"            , 5 },
		{ "làmpadas"         , 6 },
		{ "alzòlas"          , 7 },
		{ "agústu"           , 8 },
		{ "cabidànne"        , 9 },
		{ "santigaíni"       , 10 },
		{ "santandría"       , 11 },
		{ "nadàbi"           , 12 },
		// Scots
		{ "januar"            , 1 },
		{ "februar"           , 2 },
		{ "mairch"            , 3 },
		{ "apryle"            , 4 },
		{ "mey"               , 5 },
		{ "juin"              , 6 },
		{ "julie"             , 7 },
		{ "augist"            , 8 },
		{ "september"         , 9 },
		{ "october"           , 10 },
		{ "november"          , 11 },
		{ "dizember"          , 12 },
		// Scots Gaelic | Gàidhlig
		{ "an faoilteach"     , 1 },
		{ "an gearran"        , 2 },
		{ "an màrt"          , 3 },
		{ "an giblean"        , 4 },
		{ "an ceitean"        , 5 },
		{ "an t-Òg-mhios"    , 6 },
		{ "an t-luchar"       , 7 },
		{ "an lùnasdal"      , 8 },
		{ "an t-sultain"      , 9 },
		{ "an dàmhair"       , 10 },
		{ "an t-samhain"      , 11 },
		{ "an dùbhlachd"     , 12 },
		// Serbian | српски / srpski
		{ "јануар"      , 1 },
		{ "фебруар"    , 2 },
		{ "март"          , 3 },
		{ "април"        , 4 },
		{ "мај"            , 5 },
		{ "јуни"          , 6 },
		{ "јули"          , 7 },
		{ "август"      , 8 },
		{ "септембар", 9 },
		{ "октобар"    , 10 },
		{ "новембар"  , 11 },
		{ "децембар"  , 12 },
		{ "januar"            , 1 },
		{ "februar"           , 2 },
		{ "mart"              , 3 },
		{ "april"             , 4 },
		{ "maj"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "avgust"            , 8 },
		{ "septembar"         , 9 },
		{ "oktobar"           , 10 },
		{ "novembar"          , 11 },
		{ "decembar"          , 12 },
		// Seychelles Creole | seselwa
		{ "zanvye"            , 1 },
		{ "fevriye"           , 2 },
		{ "mars"              , 3 },
		{ "avril"             , 4 },
		{ "me"                , 5 },
		{ "zen"               , 6 },
		{ "zilyet"            , 7 },
		{ "out"               , 8 },
		{ "septanm"           , 9 },
		{ "oktob"             , 10 },
		{ "novanm"            , 11 },
		{ "desanm"            , 12 },
		// Shona | chiShona
		{ "ndira"             , 1 },
		{ "kukadzi"           , 2 },
		{ "kurume"            , 3 },
		{ "kubvumbi"          , 4 },
		{ "chivabvu"          , 5 },
		{ "chikumi"           , 6 },
		{ "chikunguru"        , 7 },
		{ "nyamavhuvhu"       , 8 },
		{ "gunyana"           , 9 },
		{ "gumiguru"          , 10 },
		{ "mbudzi"            , 11 },
		{ "zvita"             , 12 },
		// Sicilian | sicilianu
		{ "jinnaru"           , 1 },
		{ "frivaru"           , 2 },
		{ "marzu"             , 3 },
		{ "aprili"            , 4 },
		{ "maiu"              , 5 },
		{ "giugnu"            , 6 },
		{ "giugnettu"         , 7 },
		{ "austu"             , 8 },
		{ "sittèmmiru"       , 9 },
		{ "uttùviru"         , 10 },
		{ "nuvèmmiru"        , 11 },
		{ "dicèmmiru"        , 12 },
		// Sindhi | سنڌي (sindʰī)
		{ "جنوري"        , 1 },
		{ "فيبروري"    , 2 },
		{ "مارچ"          , 3 },
		{ "اپريل"        , 4 },
		{ "مئي"            , 5 },
		{ "جون"            , 6 },
		{ "جولاءِ"      , 7 },
		{ "آگسٽ"          , 8 },
		{ "سيپٽمبر"    , 9 },
		{ "آڪٽوبر"      , 10 },
		{ "نومبر"        , 11 },
		{ "ڊسمبر"        , 12 },
		{ "janvarī"          , 1 },
		{ "febravarī"        , 2 },
		{ "mārču"           , 3 },
		{ "aprīlu"           , 4 },
		{ "meʾī"            , 5 },
		{ "jūn"              , 6 },
		{ "jūlāʾi"         , 7 },
		{ "āgasṫu"         , 8 },
		{ "sepṫambaru"      , 9 },
		{ "ākṫobaru"       , 10 },
		{ "navambaru"         , 11 },
		{ "ḍisambaru"       , 12 },
		// Sinhalese | සිංහල (siṁhala)
		{ "ජනවාරි", 1 },
		{ "පෙබරවාරි", 2 },
		{ "මාර්තු", 3 },
		{ "අප්‍රේල්", 4 },
		{ "මැයි"      , 5 },
		{ "ජූනි"      , 6 },
		{ "ජූලි"      , 7 },
		{ "අගෝස්තු", 8 },
		{ "සැප්තැම්බර්", 9 },
		{ "ඔක්තෝබර්", 10 },
		{ "නොවැම්බර්", 11 },
		{ "දෙසැම්බර්", 12 },
		{ "janavāri"         , 1 },
		{ "pebaravāri"       , 2 },
		{ "mārtu"            , 3 },
		{ "aprēl"            , 4 },
		{ "mæyi"             , 5 },
		{ "jūni"             , 6 },
		{ "jūli"             , 7 },
		{ "agōstu"           , 8 },
		{ "sæptæmbar"       , 9 },
		{ "oktōbar"          , 10 },
		{ "novæmbar"         , 11 },
		{ "desæmbar"         , 12 },
		// Skolt Sami | sää´mǩiõll
		{ "ođđee´jjmään" , 1 },
		{ "tä´lvvmään"    , 2 },
		{ "pâ´zzlâšttammään", 3 },
		{ "njuhččmään"    , 4 },
		{ "vue´ssmään"     , 5 },
		{ "ǩie´ssmään"    , 6 },
		{ "suei´nnmään"    , 7 },
		{ "på´rǧǧmään"  , 8 },
		{ "čõhččmään"   , 9 },
		{ "kålggmään"      , 10 },
		{ "skamm’mään"    , 11 },
		{ "rosttovmään"     , 12 },
		// Slovak | slovenčina
		{ "január"           , 1 },
		{ "február"          , 2 },
		{ "marec"             , 3 },
		{ "apríl"            , 4 },
		{ "máj"              , 5 },
		{ "jún"              , 6 },
		{ "júl"              , 7 },
		{ "august"            , 8 },
		{ "september"         , 9 },
		{ "október"          , 10 },
		{ "november"          , 11 },
		{ "december"          , 12 },
		// Slovenian 1 | slovenščina
		{ "januar"            , 1 },
		{ "februar"           , 2 },
		{ "marec"             , 3 },
		{ "april"             , 4 },
		{ "maj"               , 5 },
		{ "junij"             , 6 },
		{ "julij"             , 7 },
		{ "avgust"            , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "december"          , 12 },
		// Slovio | Slovio / Словио
		{ "januar"            , 1 },
		{ "februar"           , 2 },
		{ "marc"              , 3 },
		{ "april"             , 4 },
		{ "mai"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "august"            , 8 },
		{ "septembr"          , 9 },
		{ "oktobr"            , 10 },
		{ "novembr"           , 11 },
		{ "decembr"           , 12 },
		{ "Йануар"      , 1 },
		{ "Фебруар"    , 2 },
		{ "Марц"          , 3 },
		{ "Април"        , 4 },
		{ "Маи"            , 5 },
		{ "Йуни"          , 6 },
		{ "Йули"          , 7 },
		{ "Аугуст"      , 8 },
		{ "Септембр"  , 9 },
		{ "Октобр"      , 10 },
		{ "Новембр"    , 11 },
		{ "Децембр"    , 12 },
		// Somali | Soomaaliga
		{ "jannaayo"          , 1 },
		{ "febraayo"          , 2 },
		{ "maarso"            , 3 },
		{ "abriil"            , 4 },
		{ "abriile"           , 4 },
		{ "maajo"             , 5 },
		{ "juunyo"            , 6 },
		{ "juun"              , 6 },
		{ "luulyo"            , 7 },
		{ "agoosto"           , 8 },
		{ "septembar"         , 9 },
		{ "setembar"          , 9 },
		{ "sibtambar"         , 9 },
		{ "oktoobar"          , 10 },
		{ "otoobar"           , 10 },
		{ "noofembar"         , 11 },
		{ "disembar"          , 12 },
		// Sotho | seSotho
		{ "pherekgong"        , 1 },
		{ "hlakola"           , 2 },
		{ "hlakubele"         , 3 },
		{ "mmesa"             , 4 },
		{ "motsheanong"       , 5 },
		{ "phupjane"          , 6 },
		{ "phupu"             , 7 },
		{ "phato"             , 8 },
		{ "lwetse"            , 9 },
		{ "mphalane"          , 10 },
		{ "pudungwana"        , 11 },
		{ "tshitwe"           , 12 },
		// Southern Sami | åarjelsaemiengïele
		{ "tsïengele"        , 1 },
		{ "goevte"            , 2 },
		{ "njoktje"           , 3 },
		{ "voerhtje"          , 4 },
		{ "suehpede"          , 5 },
		{ "ruffie"            , 6 },
		{ "snjaltje"          , 7 },
		{ "mïetske"          , 8 },
		{ "skïerede"         , 9 },
		{ "golke"             , 10 },
		{ "rahka"             , 11 },
		{ "goeve"             , 12 },
		// Spanish | español
		{ "enero"             , 1 },
		{ "febrero"           , 2 },
		{ "marzo"             , 3 },
		{ "abril"             , 4 },
		{ "mayo"              , 5 },
		{ "junio"             , 6 },
		{ "julio"             , 7 },
		{ "agosto"            , 8 },
		{ "septiembre"        , 9 },
		{ "octubre"           , 10 },
		{ "noviembre"         , 11 },
		{ "diciembre"         , 12 },
		// Sundanese | basa Sunda
		{ "januari"           , 1 },
		{ "pébruari"         , 2 },
		{ "maret"             , 3 },
		{ "april"             , 4 },
		{ "méi"              , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "agustus"           , 8 },
		{ "séptémber"       , 9 },
		{ "oktober"           , 10 },
		{ "nopémber"         , 11 },
		{ "désémber"        , 12 },
		// Swahili | kiswahili
		{ "januari"           , 1 },
		{ "februari"          , 2 },
		{ "machi"             , 3 },
		{ "aprili"            , 4 },
		{ "mei"               , 5 },
		{ "juni"              , 6 },
		{ "julai"             , 7 },
		{ "agosti"            , 8 },
		{ "septemba"          , 9 },
		{ "oktoba"            , 10 },
		{ "novemba"           , 11 },
		{ "desemba"           , 12 },
		// Swati | siSwati
		{ "bhimbidvwane"      , 1 },
		{ "indlovana"         , 2 },
		{ "indlovu-lenkhulu"  , 3 },
		{ "mabasa"            , 4 },
		{ "inkhwekhweti"      , 5 },
		{ "inhlaba"           , 6 },
		{ "kholwane"          , 7 },
		{ "ingci"             , 8 },
		{ "inyoni"            , 9 },
		{ "imphala"           , 10 },
		{ "lidvuba"           , 11 },
		{ "lweti"             , 11 },
		{ "ingongoni"         , 12 },
		// Swedish 1 | svenska
		{ "januari"           , 1 },
		{ "februari"          , 2 },
		{ "mars"              , 3 },
		{ "april"             , 4 },
		{ "maj"               , 5 },
		{ "juni"              , 6 },
		{ "juli"              , 7 },
		{ "augusti"           , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "december"          , 12 },
		// Syriac 1 (Common) | ܣܘܪܝܐܝܐ (sūryāyā)
		{ "ܟܢܘܢ ܐܚܪܝ" , 1 },
		{ "ܫܒܛ"            , 2 },
		{ "ܐܕܪ"            , 3 },
		{ "ܢܝܣܢ"          , 4 },
		{ "ܐܝܪ"            , 5 },
		{ "ܚܙܝܪܢ"        , 6 },
		{ "ܬܡܘܙ"          , 7 },
		{ "ܐܒ"              , 8 },
		{ "ܐܝܠܘܠ"        , 9 },
		{ "ܐܠܘܠ"          , 9 },
		{ "ܬܫܪܝܢ ܩܕܡ" , 10 },
		{ "ܬܫܪܝܢ ܩܕܝܡ", 10 },
		{ "ܬܫܪܝܢ ܐܚܪܝ", 11 },
		{ "ܟܢܘܢ ܩܕܡ"   , 12 },
		{ "ܟܢܘܢ ܩܕܝܡ" , 12 },
		{ "kānūn [’]ḥərāy", 1 },
		{ "šəḇāṭ"      , 2 },
		{ "āḏār"          , 3 },
		{ "nīsān"           , 4 },
		{ "īyār"            , 5 },
		{ "ḥəzīrān"      , 6 },
		{ "tāmūz"           , 7 },
		{ "āḇ"             , 8 },
		{ "ẹ̄lūl"          , 9 },
		{ "tešrī[n] qəḏem", 10 },
		{ "tešrī[n] qəḏīm", 10 },
		{ "tešrī[n] [’]ḥərāy", 11 },
		{ "kānūn qəḏem"  , 12 },
		{ "kānūn qəḏīm" , 12 },
		// Tagalog
		{ "enero"             , 1 },
		{ "pebrero"           , 2 },
		{ "marso"             , 3 },
		{ "abril"             , 4 },
		{ "mayo"              , 5 },
		{ "hunyo"             , 6 },
		{ "hulyo"             , 7 },
		{ "agosto"            , 8 },
		{ "setyembre"         , 9 },
		{ "oktubre"           , 10 },
		{ "nobyembre"         , 11 },
		{ "disyembre"         , 12 },
		// Tahitian | reo Tahiti
		{ "tenuare"           , 1 },
		{ "fepuare"           , 2 },
		{ "māti"             , 3 },
		{ "ʻeperēra"        , 4 },
		{ "mē"               , 5 },
		{ "tiunu"             , 6 },
		{ "tiurai"            , 7 },
		{ "ʻātete"          , 8 },
		{ "tetepa"            , 9 },
		{ "ʻatopa"           , 10 },
		{ "noema"             , 11 },
		{ "novema"            , 11 },
		{ "titema"            , 12 },
		// Taiwanese | å°ç£è©± (tâi-oân-oē)
		{ "ä¸æ"            , 1 },
		{ "äºæ"            , 2 },
		{ "ä¸æ"            , 3 },
		{ "åæ"            , 4 },
		{ "äºæ"            , 5 },
		{ "å­æ"            , 6 },
		{ "ä¸æ"            , 7 },
		{ "å«æ"            , 8 },
		{ "ä¹æ"            , 9 },
		{ "åæ"            , 10 },
		{ "åä¸æ"         , 11 },
		{ "åäºæ"         , 12 },
		{ "it-gue̍h"         , 1 },
		{ "jī-gue̍h"        , 2 },
		{ "saⁿ-gue̍h"      , 3 },
		{ "sì-gue̍h"        , 4 },
		{ "gơ̅-gue̍h"      , 5 },
		{ "la̍k-gue̍h"      , 6 },
		{ "chit-gue̍h"       , 7 },
		{ "peh-gue̍h"        , 8 },
		{ "káu-gue̍h"       , 9 },
		{ "tsa̍p-gue̍h"     , 10 },
		{ "tsa̍p-it-gue̍h"  , 11 },
		{ "tsa̍p-jī-gue̍h" , 12 },
		// Tajik | тоҷикӣ (toǧikī) / تاجیکی (tōjīkī)
		{ "январ"        , 1 },
		{ "феврал"      , 2 },
		{ "март"          , 3 },
		{ "апрел"        , 4 },
		{ "май"            , 5 },
		{ "июн"            , 6 },
		{ "июл"            , 7 },
		{ "август"      , 8 },
		{ "сентябр"    , 9 },
		{ "октябр"      , 10 },
		{ "ноябр"        , 11 },
		{ "декабр"      , 12 },
		{ "janvar"            , 1 },
		{ "fevral"            , 2 },
		{ "mart"              , 3 },
		{ "aprel"             , 4 },
		{ "maj"               , 5 },
		{ "ijun"              , 6 },
		{ "ijul"              , 7 },
		{ "avgust"            , 8 },
		{ "sentjabr"          , 9 },
		{ "oktjabr"           , 10 },
		{ "nojabr"            , 11 },
		{ "dekabr"            , 12 },
		{ "ینور"          , 1 },
		{ "فیورل"        , 2 },
		{ "مرت"            , 3 },
		{ "اپریل"        , 4 },
		{ "می"              , 5 },
		{ "اییون"        , 6 },
		{ "اییول"        , 7 },
		{ "اوگوست"      , 8 },
		{ "سینتیبر"    , 9 },
		{ "آکتیبر"      , 10 },
		{ "نایبر"        , 11 },
		{ "دیکبر"        , 12 },
		{ "yanvar"            , 1 },
		{ "fevral"            , 2 },
		{ "mart"              , 3 },
		{ "aprel"             , 4 },
		{ "mai"               , 5 },
		{ "īyūn"            , 6 },
		{ "īyūl"            , 7 },
		{ "augūst"           , 8 },
		{ "sentyabr"          , 9 },
		{ "ōktyabr"          , 10 },
		{ "nōyabr"           , 11 },
		{ "dekabr"            , 12 },
		// Tamazight (Libya) | ⵜⴰⵎⴰⵣⵉⵖⵜ / tamaziγt
		{ "ⵢⴻⵏⴰⵕ"   , 1 },
		{ "ⴼⵓⵕⴰⵕ"   , 2 },
		{ "ⵎⴰⵔⴻⵙ"   , 3 },
		{ "ⵢⴻⴱⵔⵉⵔ", 4 },
		{ "ⵎⴰⵢⵓ"      , 5 },
		{ "ⵢⵓⵏⵢⵓ"   , 6 },
		{ "ⵢⵓⵍⵢⵓⵣ", 7 },
		{ "ⵖⵓⵛⴻⵜ"   , 8 },
		{ "ⵛⵜⴻⵏⴱⴻⵕ", 9 },
		{ "ⵜⵓⴱⴻⵕ"   , 10 },
		{ "ⵓⵡⴻⵏⴱⵉⵔ", 11 },
		{ "ⴷⵉⵊⴻⵏⴱⵉⵔ", 12 },
		{ "yenaṛ"           , 1 },
		{ "fuṛaṛ"         , 2 },
		{ "mares"             , 3 },
		{ "yebrir"            , 4 },
		{ "mayu"              , 5 },
		{ "yunyu"             , 6 },
		{ "yulyuz"            , 7 },
		{ "γušet"           , 8 },
		{ "štenbeṛ"        , 9 },
		{ "tubeṛ"           , 10 },
		{ "uwenbir"           , 11 },
		{ "diženbir"         , 12 },
		// Tamil | தமிழ் (tamiḻ)
		{ "ஜனவரி"   , 1 },
		{ "பெப்ரவரி", 2 },
		{ "பிப்ரவரி", 2 },
		{ "பெப்ருவரி", 2 },
		{ "மார்ச்", 3 },
		{ "மார்சு", 3 },
		{ "ஏப்ரல்", 4 },
		{ "ஏப்ரில்", 4 },
		{ "மே"            , 5 },
		{ "ஜூன்"      , 6 },
		{ "ஜூலை"      , 7 },
		{ "ஆகஸ்ட்", 8 },
		{ "ஆகஸ்டு", 8 },
		{ "செப்டம்பர்", 9 },
		{ "செப்டெம்பர்", 9 },
		{ "அக்டோபர்", 10 },
		{ "நவம்பர்", 11 },
		{ "நொவம்பர்", 11 },
		{ "டிசம்பர்", 12 },
		{ "jaṉavari"        , 1 },
		{ "pepravari"         , 2 },
		{ "pipravari"         , 2 },
		{ "pepruvari"         , 2 },
		{ "mārč"            , 3 },
		{ "mārču"           , 3 },
		{ "ēpral"            , 4 },
		{ "ēpril"            , 4 },
		{ "mē"               , 5 },
		{ "jūṉ"            , 6 },
		{ "jūlai"            , 7 },
		{ "ākasṭ"          , 8 },
		{ "ākasṭu"         , 8 },
		{ "čepṭampar"      , 9 },
		{ "čepṭempar"      , 9 },
		{ "akṭōpar"        , 10 },
		{ "navampar"          , 11 },
		{ "novampar"          , 11 },
		{ "ṭičampar"       , 12 },
		// Tatar | татарча / tatarça
		{ "гыйнвар"    , 1 },
		{ "февраль"    , 2 },
		{ "март"          , 3 },
		{ "апрель"      , 4 },
		{ "май"            , 5 },
		{ "июнь"          , 6 },
		{ "июль"          , 7 },
		{ "август"      , 8 },
		{ "сентябрь"  , 9 },
		{ "октябрь"    , 10 },
		{ "ноябрь"      , 11 },
		{ "декабрь"    , 12 },
		{ "ğıynvar"         , 1 },
		{ "fevral"            , 2 },
		{ "mart"              , 3 },
		{ "aprel"             , 4 },
		{ "may"               , 5 },
		{ "iyun"              , 6 },
		{ "iyul"              , 7 },
		{ "avgust"            , 8 },
		{ "sentyabr"          , 9 },
		{ "oktyabr"           , 10 },
		{ "noyabr"            , 11 },
		{ "dekabr"            , 12 },
		// Telugu | తెలుగు (telugu)
		{ "జనవరి"   , 1 },
		{ "ఫిబ్రవరి", 2 },
		{ "మార్చి", 3 },
		{ "ఏప్రిల్", 4 },
		{ "మే"            , 5 },
		{ "జూన్"      , 6 },
		{ "జూలై"      , 7 },
		{ "ఆగస్టు", 8 },
		{ "సెప్టెంబర్", 9 },
		{ "అక్టోబర్", 10 },
		{ "నవంబర్", 11 },
		{ "డిసెంబర్", 12 },
		{ "janvari"           , 1 },
		{ "pʰibravari"       , 2 },
		{ "mārči"           , 3 },
		{ "ēpril"            , 4 },
		{ "mē"               , 5 },
		{ "jūn"              , 6 },
		{ "jūlai"            , 7 },
		{ "āgasṭu"         , 8 },
		{ "sepṭeṁbar"     , 9 },
		{ "akṭōbar"        , 10 },
		{ "navaṁbar"        , 11 },
		{ "ḍisaṁbar"      , 12 },
		// Tetum | tetun
		{ "janeiru"           , 1 },
		{ "fevereiru"         , 2 },
		{ "marsu"             , 3 },
		{ "abríl"            , 4 },
		{ "maiu"              , 5 },
		{ "juñu"             , 6 },
		{ "jullu"             , 7 },
		{ "agostu"            , 8 },
		{ "setembru"          , 9 },
		{ "outubru"           , 10 },
		{ "novembru"          , 11 },
		{ "dezembru"          , 12 },
		// Thai 1 | ภาษาไทย (pʰāsā tʰai[y])
		{ "มกราคม", 1 },
		{ "กุมภาพันธ์", 2 },
		{ "มีนาคม", 3 },
		{ "เมษายน", 4 },
		{ "พฤษภาคม", 5 },
		{ "มิถุนายน", 6 },
		{ "กรกฎาคม", 7 },
		{ "สิงหาคม", 8 },
		{ "กันยายน", 9 },
		{ "ตุลาคม", 10 },
		{ "พฤศจิกายน", 11 },
		{ "ธันวาคม", 12 },
		{ "mokarākʰom"      , 1 },
		{ "kumpāpʰân[t]"   , 2 },
		{ "mīnākʰom"       , 3 },
		{ "mēsāyon"         , 4 },
		{ "prʉtpʰākʰom"   , 5 },
		{ "mitʰunāyon"      , 6 },
		{ "karakadākʰom"    , 7 },
		{ "siṅhākʰom"     , 8 },
		{ "kânyāyon"        , 9 },
		{ "tulākʰom"        , 10 },
		{ "prʉtčikāyon"    , 11 },
		{ "tʰânwākʰom"    , 12 },
		// Tibetan | བོད་སྐད་ (bod.skad.)
		{ "ཟླ་དང་པོ་", 1 },
		{ "ཟླ་གཉིས་པ་", 2 },
		{ "ཟླ་གསུམ་པ་", 3 },
		{ "ཟླ་བཞི་པ་", 4 },
		{ "ཟླ་ལྔ་པ་", 5 },
		{ "ཟླ་དྲུག་པ་", 6 },
		{ "ཟླ་བདུན་པ་", 7 },
		{ "ཟླ་བརྒྱད་པ་", 8 },
		{ "ཟླ་དགུ་པ་", 9 },
		{ "ཟླ་བཅུ་པ་", 10 },
		{ "ཟླ་བཅུ་གཅིག་པ་", 11 },
		{ "ཟླ་བཅུ་གཉིས་པ་", 12 },
		{ "zla.daṅ.po."     , 1 },
		{ "zla.gñis.pa."     , 2 },
		{ "zla.gsum.pa."      , 3 },
		{ "zla.bži.pa."      , 4 },
		{ "zla.lṅa.pa."     , 5 },
		{ "zla.drug.pa."      , 6 },
		{ "zla.bdun.pa."      , 7 },
		{ "zla.brgyad.pa."    , 8 },
		{ "zla.dgu.pa."       , 9 },
		{ "zla.bču.pa."      , 10 },
		{ "zla.bču.gčig.pa.", 11 },
		{ "zla.bču.gñis.pa.", 12 },
		// Tok Pisin
		{ "janueri"           , 1 },
		{ "februeri"          , 2 },
		{ "mas"               , 3 },
		{ "epril"             , 4 },
		{ "me"                , 5 },
		{ "jun"               , 6 },
		{ "julai"             , 7 },
		{ "ogas"              , 8 },
		{ "septemba"          , 9 },
		{ "oktoba"            , 10 },
		{ "novemba"           , 11 },
		{ "disemba"           , 12 },
		// Tongan | faka-Tonga
		{ "sanuali"           , 1 },
		{ "fepueli"           , 2 },
		{ "maʻasi"           , 3 },
		{ "ʻepeleli"         , 4 },
		{ "me"                , 5 },
		{ "sune"              , 6 },
		{ "siulai"            , 7 },
		{ "ʻaokosi"          , 8 },
		{ "sepitema"          , 9 },
		{ "ʻokatopa"         , 10 },
		{ "novema"            , 11 },
		{ "tisema"            , 12 },
		// Tsonga 1 | xiTsonga
		{ "janiwari"          , 1 },
		{ "febriwari"         , 2 },
		{ "machi"             , 3 },
		{ "apireli"           , 4 },
		{ "meyi"              , 5 },
		{ "juni"              , 6 },
		{ "julayi"            , 7 },
		{ "agoste"            , 8 },
		{ "septembere"        , 9 },
		{ "oktoba"            , 10 },
		{ "novhemba"          , 11 },
		{ "disemba"           , 12 },
		// Turkish | Türkçe
		{ "ocak"              , 1 },
		{ "şubat"            , 2 },
		{ "mart"              , 3 },
		{ "nisan"             , 4 },
		{ "mayıs"            , 5 },
		{ "haziran"           , 6 },
		{ "temmuz"            , 7 },
		{ "ağustos"          , 8 },
		{ "eylül"            , 9 },
		{ "ekim"              , 10 },
		{ "kasım"            , 11 },
		{ "aralık"           , 12 },
		// Turkmen 1 | türkmen / түркмен
		{ "türkmenbaşy"     , 1 },
		{ "baýdak"           , 2 },
		{ "nowruz"            , 3 },
		{ "gurbansoltan"      , 4 },
		{ "magtymguly"        , 5 },
		{ "oguz"              , 6 },
		{ "gorkut"            , 7 },
		{ "alparslan"         , 8 },
		{ "ruhnama"           , 9 },
		{ "garaşsyzlyk"      , 10 },
		{ "sanjar"            , 11 },
		{ "bitaraplyk"        , 12 },
		{ "түркменбашы", 1 },
		{ "байдак"      , 2 },
		{ "новруз"      , 3 },
		{ "гурбансолтан", 4 },
		{ "магтымгулы", 5 },
		{ "огуз"          , 6 },
		{ "горкут"      , 7 },
		{ "алпарслан", 8 },
		{ "рухнама"    , 9 },
		{ "гарашсызлык", 10 },
		{ "санҗар"      , 11 },
		{ "битараплык", 12 },
		// Tuvan 1 | тыва (tyva) see rus
		{ "бир ай"       , 1 },
		{ "ийи ай"       , 2 },
		{ "үш ай"         , 3 },
		{ "дөрт ай"     , 4 },
		{ "беш ай"       , 5 },
		{ "алды ай"     , 6 },
		{ "чеди ай"     , 7 },
		{ "сес ай"       , 8 },
		{ "тос ай"       , 9 },
		{ "он ай"         , 10 },
		{ "он бир ай"  , 11 },
		{ "он ийи ай"  , 12 },
		{ "bir aj"            , 1 },
		{ "iji aj"            , 2 },
		{ "üš aj"           , 3 },
		{ "dört aj"          , 4 },
		{ "beš aj"           , 5 },
		{ "aldy aj"           , 6 },
		{ "čedi aj"          , 7 },
		{ "ses aj"            , 8 },
		{ "tos aj"            , 9 },
		{ "on aj"             , 10 },
		{ "on bir aj"         , 11 },
		{ "on iji aj"         , 12 },
		// Ukrainian | українська (ukraïns'ka)
		{ "січень"      , 1 },
		{ "лютий"        , 2 },
		{ "березень"  , 3 },
		{ "квітень"    , 4 },
		{ "травень"    , 5 },
		{ "червень"    , 6 },
		{ "липень"      , 7 },
		{ "серпень"    , 8 },
		{ "вересень"  , 9 },
		{ "жовтень"    , 10 },
		{ "листопад"  , 11 },
		{ "грудень"    , 12 },
		{ "sičen'"           , 1 },
		{ "ljutyj"            , 2 },
		{ "berezen'"          , 3 },
		{ "kviten'"           , 4 },
		{ "traven'"           , 5 },
		{ "červen'"          , 6 },
		{ "lypen'"            , 7 },
		{ "serpen'"           , 8 },
		{ "veresen'"          , 9 },
		{ "žovten'"          , 10 },
		{ "lystopad"          , 11 },
		{ "hruden'"           , 12 },
		// Upper Sorbian 1 | hornjoserbšćina
		{ "wulki róžk"      , 1 },
		{ "mały róžk"      , 2 },
		{ "nalětnik"         , 3 },
		{ "jutrownik"         , 4 },
		{ "róžownik"        , 5 },
		{ "smažnik"          , 6 },
		{ "pražnik"          , 7 },
		{ "žnjenc"           , 8 },
		{ "požnjenc"         , 9 },
		{ "winowc"            , 10 },
		{ "nazymnik"          , 11 },
		{ "hodownik"          , 12 },
		// Urdu | اردو (urdū)
		{ "جنوری"        , 1 },
		{ "فروری"        , 2 },
		{ "مارچ"          , 3 },
		{ "اپریل"        , 4 },
		{ "مئ"              , 5 },
		{ "جون"            , 6 },
		{ "جولائی"      , 7 },
		{ "اگست"          , 8 },
		{ "ستمبر"        , 9 },
		{ "اکتوبر"      , 10 },
		{ "نومبر"        , 11 },
		{ "دسمبر"        , 12 },
		{ "janvarī"          , 1 },
		{ "farvarī"          , 2 },
		{ "mārč"            , 3 },
		{ "aprīl"            , 4 },
		{ "maʾi"             , 5 },
		{ "jūn"              , 6 },
		{ "jūlāʾī"        , 7 },
		{ "agast"             , 8 },
		{ "sitambar"          , 9 },
		{ "aktūbar"          , 10 },
		{ "navambar"          , 11 },
		{ "disambar"          , 12 },
		// Uyghur | ئۇيغۇرچە / uyghurche
		{ "يانۋار"      , 1 },
		{ "فېۋرال"      , 2 },
		{ "مارت"          , 3 },
		{ "ئاپرېل"      , 4 },
		{ "ماي"            , 5 },
		{ "ئىيۇن"        , 6 },
		{ "ئىيۇل"        , 7 },
		{ "ئاۋغۇست"    , 8 },
		{ "سېنتەبىر"  , 9 },
		{ "ئۆكتەبىر"  , 10 },
		{ "نويابىر"    , 11 },
		{ "دېكابىر"    , 12 },
		{ "yanwar"            , 1 },
		{ "féwral"           , 2 },
		{ "mart"              , 3 },
		{ "aprél"            , 4 },
		{ "may"               , 5 },
		{ "iyun"              , 6 },
		{ "iyul"              , 7 },
		{ "awghust"           , 8 },
		{ "séntebir"         , 9 },
		{ "öktebir"          , 10 },
		{ "noyabir"           , 11 },
		{ "dékabir"          , 12 },
		// Uzbek | oʻzbek / ўзбек
		{ "yanvar"            , 1 },
		{ "fevral"            , 2 },
		{ "mart"              , 3 },
		{ "aprel"             , 4 },
		{ "may"               , 5 },
		{ "iyun"              , 6 },
		{ "iyul"              , 7 },
		{ "avgust"            , 8 },
		{ "sentyabr"          , 9 },
		{ "oktyabr"           , 10 },
		{ "noyabr"            , 11 },
		{ "dekabr"            , 12 },
		{ "январь"      , 1 },
		{ "февраль"    , 2 },
		{ "март"          , 3 },
		{ "апрель"      , 4 },
		{ "май"            , 5 },
		{ "июнь"          , 6 },
		{ "июль"          , 7 },
		{ "август"      , 8 },
		{ "сентябрь"  , 9 },
		{ "октябрь"    , 10 },
		{ "ноябрь"      , 11 },
		{ "декабрь"    , 12 },
		// Venda | tshiVenḓa
		{ "phando"            , 1 },
		{ "luhuhi"            , 2 },
		{ "Ṱhafamuhwe"      , 3 },
		{ "lambamai"          , 4 },
		{ "shundunthule"      , 5 },
		{ "fulwi"             , 6 },
		{ "fulwana"           , 7 },
		{ "Ṱhangule"        , 8 },
		{ "khubvumedzi"       , 9 },
		{ "tshimedzi"         , 10 },
		{ "Ḽara"            , 11 },
		{ "nyendavhusiku"     , 12 },
		// Venedic | Wenedyk
		{ "jąwarz"           , 1 },
		{ "fiewrarz"          , 2 },
		{ "marć"             , 3 },
		{ "oprzyl"            , 4 },
		{ "maj"               , 5 },
		{ "juń"              , 6 },
		{ "jul"               , 7 },
		{ "ugust"             , 8 },
		{ "sieciębierz"      , 9 },
		{ "ocębierz"         , 10 },
		{ "nowiębierz"       , 11 },
		{ "dzieczębierz"     , 12 },
		// Vietnamese | tiếng Việt
		{ "tháng một"      , 1 },
		{ "tháng hai"        , 2 },
		{ "tháng ba"         , 3 },
		{ "tháng tư"        , 4 },
		{ "tháng năm"       , 5 },
		{ "tháng sáu"       , 6 },
		{ "tháng bảy"      , 7 },
		{ "tháng tám"       , 8 },
		{ "tháng chín"      , 9 },
		{ "tháng mười"    , 10 },
		{ "tháng mười một", 11 },
		{ "tháng mười hai", 12 },
		// Volapük 1
		{ "balul"             , 1 },
		{ "telul"             , 2 },
		{ "kilul"             , 3 },
		{ "folul"             , 4 },
		{ "lulul"             , 5 },
		{ "mälul"            , 6 },
		{ "velul"             , 7 },
		{ "jölul"            , 8 },
		{ "zülul"            , 9 },
		{ "balsul"            , 10 },
		{ "babul"             , 11 },
		{ "balsebalul"        , 11 },
		{ "batul"             , 12 },
		{ "balsetelul"        , 12 },
		// Voro 1 | võro
		{ "vahtsõaastakuu"   , 1 },
		{ "radokuu"           , 2 },
		{ "urbõkuu"          , 3 },
		{ "mahlakuu"          , 4 },
		{ "lehekuu"           , 5 },
		{ "piimäkuu"         , 6 },
		{ "hainakuu"          , 7 },
		{ "põimukuu"         , 8 },
		{ "süküskuu"        , 9 },
		{ "rehekuu"           , 10 },
		{ "märtekuu"         , 11 },
		{ "joulukuu"          , 12 },
		// Walloon | walon
		{ "djanvî"           , 1 },
		{ "fevrî"            , 2 },
		{ "måss"             , 3 },
		{ "avri"              , 4 },
		{ "may"               , 5 },
		{ "djun"              , 6 },
		{ "djulete"           , 7 },
		{ "awousse"           , 8 },
		{ "setimbe"           , 9 },
		{ "octôbe"           , 10 },
		{ "nôvimbe"          , 11 },
		{ "decimbe"           , 12 },
		// Welsh | Cymraeg
		{ "ionawr"            , 1 },
		{ "chwefror"          , 2 },
		{ "mawrth"            , 3 },
		{ "ebrill"            , 4 },
		{ "mai"               , 5 },
		{ "mehefin"           , 6 },
		{ "gorffennaf"        , 7 },
		{ "awst"              , 8 },
		{ "medi"              , 9 },
		{ "hydref"            , 10 },
		{ "tachwedd"          , 11 },
		{ "rhagfyr"           , 12 },
		// Wolof | wolof
		{ "samfiyee"          , 1 },
		{ "feebarye"          , 2 },
		{ "mars"              , 3 },
		{ "awril"             , 4 },
		{ "meey"              , 5 },
		{ "suwee"             , 6 },
		{ "yuuliyoo"          , 7 },
		{ "waxset"            , 8 },
		{ "ut"                , 8 },
		{ "sàttumbar"        , 9 },
		{ "oktoobar"          , 10 },
		{ "nofàmbar"         , 11 },
		{ "desàmbar"         , 12 },
		// Xhosa 1 | isiXhosa
		{ "ujanuwari"         , 1 },
		{ "ufebhruwari"       , 2 },
		{ "ufebruwari"        , 2 },
		{ "umatshi"           , 3 },
		{ "uepreli"           , 4 },
		{ "uaprili"           , 4 },
		{ "umeyi"             , 5 },
		{ "ujuni"             , 6 },
		{ "ujulayi"           , 7 },
		{ "uagasti"           , 8 },
		{ "useptemba"         , 9 },
		{ "uoktobha"          , 10 },
		{ "unovemba"          , 11 },
		{ "udisemba"          , 12 },
		// Yiddish | ייִדיש (yidiš)
		{ "יאַנואַר"  , 1 },
		{ "פֿעברואַר", 2 },
		{ "מאַרץ"        , 3 },
		{ "אַפּריל"    , 4 },
		{ "מײַ"            , 5 },
		{ "יוני"          , 6 },
		{ "יולי"          , 7 },
		{ "אױגוסט"      , 8 },
		{ "סעפּטעמבער", 9 },
		{ "אָקטאָבער", 10 },
		{ "נאָװעמבער", 11 },
		{ "דעצעמבער"  , 12 },
		{ "yanuar"            , 1 },
		{ "februar"           , 2 },
		{ "marts"             , 3 },
		{ "april"             , 4 },
		{ "may"               , 5 },
		{ "yuni"              , 6 },
		{ "yuli"              , 7 },
		{ "oygust"            , 8 },
		{ "september"         , 9 },
		{ "oktober"           , 10 },
		{ "november"          , 11 },
		{ "detsember"         , 12 },
		// Zazaki | zazaki
		{ "çele"             , 1 },
		{ "gucige"            , 2 },
		{ "adare"             , 3 },
		{ "nisane"            , 4 },
		{ "gulane"            , 5 },
		{ "hezirane"          , 6 },
		{ "temmuze"           , 7 },
		{ "tebaxe"            , 8 },
		{ "keşkelun"         , 9 },
		{ "tişrino verên"   , 10 },
		{ "tişrino peyên"   , 11 },
		{ "gağande"          , 12 },
		// Zulu 1 | isiZulu
		{ "ujanuwari"         , 1 },
		{ "ufebruwari"        , 2 },
		{ "umashi"            , 3 },
		{ "uephuleli"         , 4 },
		{ "uapreli"           , 4 },
		{ "uaphrili"          , 4 },
		{ "umeyi"             , 5 },
		{ "ujuni"             , 6 },
		{ "ujulayi"           , 7 },
		{ "uagasti"           , 8 },
		{ "usebutemba"        , 9 },
		{ "uokthoba"          , 10 },
		{ "uoktoba"           , 10 },
		{ "unovemba"          , 11 },
		{ "udisemba"          , 12 },
	
		{ "\0"                , 0  }};
	*/

// hash table of months
static HashTableX s_mt;
static char       s_mbuf [ 6000 ];

// returns -1 if not a valid month
char getMonth ( int64_t wid ) {
	// only init the table once
	static bool s_init12 = false;
	// set up the month name hashtable
	if ( ! s_init12 ) {
		// set the keysize to 8 and month size to 1 byte
		if ( ! s_mt.set( 8,1,300,s_mbuf,6000,false,0,"months")) 
			return false;
		// load month names and their values into hashtable from above
		for ( int32_t i = 0 ; *months[i].month ; i++ ) {
			char     *m    = months[i].month;
			int32_t      mlen = gbstrlen(m);
			uint64_t  h    = hash64Lower_utf8(m,mlen);
			// add should always be success since we are pre-alloc
			if ( ! s_mt.addKey(&h,&months[i].value)){
				char*xx=NULL;*xx=0;}
		}
		// do not repeat this
		s_init12 = true;
	}
	
	char *month = (char *)s_mt.getValue64 ( wid );
	// bail if no match
	if ( ! month ) return -1;
	// otherwise, return it
	return *month;
}

#define MAX_INTERVALS 30000

// . called by Events.cpp to store all the intervals for a particular date
// . TODO: we basically do this once for hashing and once for the call to
//   Events::getEventsData(), so try to fix that
// . constrain intervals to [year0,year1) year range
bool Dates::getIntervals2 ( Date *dp , 
			    SafeBuf *sb, 
			    int32_t year0 , 
			    int32_t year1 ,
			    Date **closeDates ,
			    int32_t  numCloseDates ,
			    char  timeZone ,
			    char  useDST   ,
			    Words *words ) {

	// sanity
	if ( timeZone < -13 || timeZone > 13 ) { char *xx=NULL;*xx=0; }
	if ( useDST != 0 && useDST != 1      ) { char *xx=NULL;*xx=0; }

	// set it i guess
	if ( ! m_words ) m_words = words;

	m_year0 = year0;
	m_year1 = year1;

	// if we had an assumed year, do the restriction now
	//if ( dp->m_flags & DF_ASSUMED_YEAR ) {
	//	// sanity check
	//	if ( dp->m_year <= 0 ) { char *xx=NULL;*xx=0; }
	//	m_year0 = dp->m_year;
	//	m_year1 = dp->m_year+1;
	//}


	if ( dp->m_flags & DF_ASSUMED_YEAR ) {
		// sanity check
		//if ( dp->m_year <= 0 ) { char *xx=NULL;*xx=0; }
		// dates are too old if this is true, return empty
		//if ( dp->m_year + 1 < m_year0 ) return true;
	}

	// use the telescoped date if we got that, it has more info
	//if ( dp->m_telescope ) dp = dp->m_telescope;
	//if ( dp->m_telescope ) { char *xx=NULL;*xx=0; }

	// fill in the final set of intervals for this date
	Interval finalInt [ MAX_INTERVALS + 1 ];

	// . add in the intervals for this date into m_int1
	// . each Interval is a range of time_t's like [a,b), closed on
	//   the left and open on the right.
	// . may dates in m_datePtrs are ranges, and this takes care of it
	int32_t ni = addIntervals ( dp , 0 , finalInt , 0 , dp );

	// this would have set this to -1 and g_errno on error
	if ( ni == -1 ) return false;

	// the return ptr
	Interval *retInt = finalInt;
	int32_t      retni  = ni;

	// store result here
	Interval int3 [ MAX_INTERVALS + 1 ];
	// init ptrs
	Interval *arg1 = finalInt;
	Interval *arg3 = int3;
	// this is int1
	int32_t       ni1 = ni;
	int32_t       ni3 = 0;


	// . now intersect with our 365 day assumed range
	// . m_minPubDate is from SpiderRequest::m_parentPrevSpiderTime
	//   which we use to estimate our pub date if this page's outlink was
	//   added to its parent since the last time the parent was spidered
	// . NOTE: i changed 365 to 90 days since much more than 90 days and
	//   people usually put a year in the date
	if ( (dp->m_flags & DF_ASSUMED_YEAR) ) { //dp->m_minPubDate > 0 ) {
		// wtf?
		if ( dp->m_minStartFocus == 0 ) { char *xx=NULL;*xx=0; }
		// a simple interval
		Interval simple[1];
		//simple[0].m_a = dp->m_minPubDate;
		simple[0].m_a = dp->m_minStartFocus;
		// do not change this 90*24*3600 without also changing it
		// in the line above!
		//simple[0].m_b = dp->m_minPubDate + DAYLIMIT*24*3600;
		simple[0].m_b = dp->m_maxStartFocus;
		// int1 INTERSECT simple and stored into int3.
		ni3 = intersect3 ( arg1,simple,arg3,ni1,1,0 , false,false);
		// error?
		if ( ni3 == -1 ) return false;
		// store result in case we return
		retInt = arg3;
		retni  = ni3;
		// swap for next iteration, if we do it
		Interval *tmp = arg1;
		arg1 = arg3;
		arg3 = tmp;
		ni1  = ni3;
	}


	// subtract the close dates
	for ( int32_t i = 0 ; i < numCloseDates ; i++ ) {
		// int16_tcut
		Date *cd = closeDates[i];
		// sanity check
		if ( ! ( cd->m_flags & DF_CLOSE_DATE )){char *xx=NULL;*xx=0; }
		// fill this up
		Interval int2 [ MAX_INTERVALS + 1 ];
		// subtract them!
		int32_t ni2 = addIntervals ( cd,0,int2,0,cd);
		// int1 - int2 and stored into int3. subtract = true
		ni3 = intersect3 ( arg1,int2,arg3,ni1,ni2,0 , true,false);
		// error?
		if ( ni3 == -1 ) return false;
		// just for debugging unm.edu!!
		//if ( ni3 == ni1 ) { char *xx=NULL;*xx=0; }
		// store result in case we return
		retInt = arg3;
		retni  = ni3;
		// swap for next iteration, if we do it
		Interval *tmp = arg1;
		arg1 = arg3;
		arg3 = tmp;
		ni1  = ni3;
	}

	// if date has a tod but no tod range, then set Interval::m_b to -1
	// for each Interval instead of default it to midnight. this way
	// Events.cpp knows not to set the EV_STORE_HOURS bit if it has no
	// ending time. and to set the EV_STORE_HOURS bit if it does have
	// an ending tod.
	if ( (dp->m_hasType & DT_TOD) && 
	     !(dp->m_hasType & DT_RANGE_TOD) &&
	     // fix "October 25, 2011 4:00pm  End: October 25, 2011 8:00 pm" 
	     // for http://www.seattle24x7.com/calendar/calendar.htm 
	     !(dp->m_hasType & DT_RANGE_TIMEPOINT) ) {
		// loop over every interval
		for ( int32_t i = 0 ; i < retni ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// set to -1
			retInt[i].m_b = retInt[i].m_a; // -1;
		}
	}

	// if we do not have store hours or substore hours set then
	// make the end point equal the start point so it is treat as
	// an event that starts at that time, and is pointless to showup
	// in the middle of it.
	// NO! this causes us to lose some events for rateclubs.com that
	// has some event from x to y and also has it with before 9:30
	// so that the endpoint is different. but now that the endpoint
	// was nuked from this code here, it got SPECIAL_DUP'ed out! and
	// really even though the rateclubs.com thing was not labelled
	// by us as store hours, it really was, it was substore hours, and
	// you could show up at any time...
	/*
	dateflags_t mask = DF_STORE_HOURS | DF_SUBSTORE_HOURS;
	if ( !(dp->m_flags & mask) ) {
		// loop over every interval and nuke the end time
		for ( int32_t i = 0 ; i < retni ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// set to -1
			retInt[i].m_b = retInt[i].m_a; // -1;
		}
	}
	*/

	//
	// now convert the interval times from local times into UTC
	//
	int32_t i = 0;
	int32_t j = 0;
	// timeZone is in hours, usually -5,-6,-7,-8 (EST/CST/MST/PST)
	int32_t tzoff = timeZone * 3600;
	// . now convert from local time into utc... and handle dst
	// . set then dst intervals for each year
	for ( int32_t y = m_year0 ; y <= m_year1 ; y++ ) {
		// not if facebook though! they are already in utc
		//if ( isFacebook ) break;
		// set daylight start for UTC
		int32_t daylightStart ;
		int32_t daylightEnd   ;
		// . this function is in Address.cpp above getIsDst()
		// . daylightStart is time_t in UTC when daylight savings time
		//   starts for this year.
		getDSTInterval ( y , &daylightStart, &daylightEnd );
		// get year range
		int32_t ystart = getYearMonthStart(y  ,1);
		int32_t yend   = getYearMonthStart(y+1,1);
		// breathe
		QUICKPOLL(m_niceness);
		// now scan the intervals that fall into this year
		for ( ; i < retni ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if beyond year and advance year
			if ( retInt[i].m_a >= yend ) break;
			// . sanity check
			// . this hurts "10am to 6pm - 7 days a week" for
			//   www.corsicanastorage.com
			// . retInt[i].m_b is >= ystart but the 10am part
			//   is from the previous day. so it spans the years
			//   so i added retInt[i].m_b <= ystart here
			if ( retInt[i].m_a <  ystart &&
			     retInt[i].m_b <= ystart ) { char *xx=NULL;*xx=0;}
			// convert to UTC
			retInt[i].m_a -= tzoff;
			// convert the a point (in UTC!)
			if ( useDST &&
			     retInt[i].m_a >= daylightStart &&
			     retInt[i].m_a <  daylightEnd )
				// remove DST's additional hour
				retInt[i].m_a -= 3600;
		}
		// same for b
		for ( ; j < retni ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if beyond year and advance year
			if ( retInt[j].m_b >= yend ) break;
			// convert to UTC
			retInt[j].m_b -= tzoff;
			// convert the a point (in UTC!)
			if ( useDST &&
			     retInt[j].m_b >= daylightStart &&
			     retInt[j].m_b <  daylightEnd )
				// remove DST's additional hour
				retInt[j].m_b -= 3600;
		}
	}
		

	// copy into there
	return sb->safeMemcpy ( (char *)retInt , retni * sizeof(Interval) ) ;
}

#define MIN_UNBOUNDED 1
#define MAX_UNBOUNDED 2

//#define _DLOG_ 1

// . returns -1 and sets g_errno on error
// . otherwise returns # of intervals stored into "retInt" array
// . every date generates time intervals whose endpoints are in seconds since 
//   the epoch (jan 1, 1970)
// . we restrict to the spidered year and the following year to save resources
//   so if someone said "every wednesday" we'd only add up to two years worth
//   of "wednesday intervals" to the tree
// . adds intervals to m_tree
// . TODO: SUPPORT: "nov 1 7pm - nov 3 8pm" will have an end tod > 1 day
// . TODO: SUPPORT: "dev 11,12, 15 jan 4" (list of two MONTH|DAYNUMs where
//                  one has a list of daynums...
int32_t Dates::addIntervals ( Date       *di     ,
			   char        hflag  ,
			   // fill up this buffer with the intervals
			   Interval   *retInt ,
			   int32_t        depth  ,
			   Date       *orig   ) {

	int32_t ni = addIntervalsB ( di,hflag,retInt,depth,orig);

	// . now if we had the word "non" before the date then we must
	//   complement the intervals.
	// . fixes "non-holiday mondays" for collectorsguide.com
	if ( ! (di->m_suppFlags & SF_NON) ) return ni;
	// return if none
	if ( ni == 0 ) return ni;
	// copy into this buffer
	Interval buf[MAX_INTERVALS];
	if ( ni > MAX_INTERVALS ) ni = MAX_INTERVALS;
	gbmemcpy(buf,retInt,ni*sizeof(Interval));

	// store here
	Interval *dst = retInt;
	int32_t j = 0;
	// complement them
	for ( int32_t i = 0 ; i < ni ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// finish last one
		if ( i == 0 ) {
			dst[j].m_a = 0;
			dst[j].m_b = buf[i].m_a;
			j++;
		}
		dst[j].m_a = buf[i].m_b;
		if ( i + 1 < ni ) dst[j].m_b = buf[i+1].m_a;
		else              dst[j].m_b = 0x7fffffff; // MAX_DATE;
		j++;
	}
	return j;
}

//int32_t nd = s_numDaysInMonth[m-1];
char s_numDaysInMonth [] = {31,28,31,30,31,30,31, 31,30,31,30,31};

// month = 0 to 11. 0=jan 1=feb ...
int32_t getNumDaysInMonth ( int32_t month , int32_t year ) {
	// sanity. month is 0 to 11, not 1 to 12
	if ( month >= 12 ) { char *xx=NULL;*xx=0; }
	// sanity more. year is like 1900+
	if ( year < 100 ) { char  *xx=NULL;*xx=0; }
	// get days in month
	int32_t nd = s_numDaysInMonth[month];
	// are we a leap year?
	bool isLeapYear = ( (year % 4) == 0 );
	// but every century we skip a leap year
	// -- unless divisble by 400... (wikipedia)
	if ( (year % 100) == 0 && (year % 400) != 0 ) 
		isLeapYear = false;
	// feb and a leap year? 
	if ( month == 1 && isLeapYear ) nd++;
	return nd;
}


int32_t Dates::addIntervalsB ( Date       *di     ,
			   char        hflag  ,
			   // fill up this buffer with the intervals
			   Interval   *retInt ,
			   int32_t        depth  ,
			   Date       *orig   ) {

	// int16_tcut
	char *u = ""; if ( m_url ) u = m_url->getUrl();

	// each simple date type uses this to store its intervals before
	// intersecting
	Interval  tmp1[MAX_INTERVALS];
	Interval *int1 = tmp1;
	int32_t      ni1  = 0;

	// and we intersect those with m_int2, the accumulator
	Interval  tmp2[MAX_INTERVALS];
	Interval *int2 = tmp2;
	int32_t      ni2  = 0;

	// . and store intersection into m_int3
	// . and right after swap m_int2 with m_int3
	Interval *int3 = retInt;
	int32_t      ni3  = 0;

	// . quick skip if totally wrong year
	// . fixes obits.abqjournal.com some
	if ( di->m_year >= 0 && di->m_year < m_year0 ) return 0;
	if ( di->m_year >= 0 && di->m_year > m_year1 ) return 0;

	// this range algo only works on simple date types for now
	datetype_t simpleFlags = DT_DOW|DT_DAYNUM|DT_MONTH|DT_YEAR;

#ifdef _DLOG_	
	// debug log
	char *ps = "unknown";
	if ( di->m_type == DT_TOD ) ps = "tod";
	if ( di->m_type == DT_DAYNUM ) ps = "daynum";
	if ( di->m_type == DT_MONTH ) ps = "month";
	if ( di->m_type == DT_YEAR ) ps = "year";
	if ( di->m_type == DT_DOW ) ps = "dow";
	if ( di->m_type == DT_HOLIDAY ) ps = "holiday";
	if ( di->m_type == DT_SUBDAY ) ps = "subday";
	if ( di->m_type == DT_SUBWEEK ) ps = "subweek";
	if ( di->m_type == DT_SUBMONTH ) ps = "submonth";
	if ( di->m_type == DT_EVERY_DAY ) ps = "everyday";
	if ( di->m_type == DT_SEASON ) ps = "season";
	if ( di->m_type == DT_ALL_HOLIDAYS ) ps = "allholidays";
	if ( di->m_type == DT_TIMESTAMP ) ps = "timestamp";
	if ( di->m_type == DT_RANGE ) ps = "range";
	if ( di->m_type == DT_LIST_DAYNUM ) ps = "listdaynum";
	if ( di->m_type == DT_LIST_MONTH ) ps = "listmonth";
	if ( di->m_type == DT_LIST_MONTHDAY ) ps = "listmonthday";
	if ( di->m_type == DT_LIST_DOW ) ps = "listdow";
	if ( di->m_type == DT_LIST_TOD ) ps = "listtod";
	if ( di->m_type == DT_LIST_OTHER ) ps = "listother";
	if ( di->m_type == DT_COMPOUND ) ps = "compound";
	if ( di->m_type == DT_TELESCOPE ) ps = "telescope";
	if ( di->m_type == DT_RANGE_TOD ) ps = "rangetod";
	if ( di->m_type == DT_RANGE_DOW ) ps = "rangedow";
	if ( di->m_type == DT_RANGE_YEAR ) ps = "rangeyear";
	// a depth indicator
	char ds[40];
	for ( int32_t k = 0 ; k < depth ; k++ ) ds[k]='-';
	ds[depth]='\0';
	logf(LOG_DEBUG,"dates: %s adding intervals for date type %s num=%"INT32"",
	     ds,ps,di->m_num);
#endif

	// . do range intersections ourselves since they are tricky
	// . "july 2nd 8pm - dec 3rd 3pm" (complex range)
	// . "8pm - 9pm" (simple range)
	// . "2008 - 2010" (simple range)
	if ( di->m_type & DT_RANGE_ANY ) {
		// must have just two ptrs to make a range
		if ( di->m_numPtrs != 2 ) { char *xx=NULL;*xx=0; }
		// add in the associated intervals for this complex date
		// into either int1 or int2, if int1 is occupied
		ni1 = addIntervals ( di->m_ptrs[0], 0, int1 , depth+1, orig);
		ni2 = addIntervals ( di->m_ptrs[1], 0, int2 , depth+1, orig);
		// point to the two sets of intervals
		Interval *tmp1 = int1;
		Interval *tmp2 = int2;
		// . fix "Tuesday-Sunday"
		// . swap the two sets if range is backwards
		// . no! that would be the complement of what we want!
		if ( di->m_ptrs[0]->m_num > di->m_ptrs[1]->m_num ) {
			//tmp1 = int2;
			//tmp2 = int1;
			// add this to the right end point
			//addOff = 7*3600*24;
		}
		// fix oct 15 - march 15
		if ( di->m_ptrs[0]->m_month > di->m_ptrs[1]->m_month &&
		     di->m_ptrs[0]->m_year == -1 &&
		     di->m_ptrs[1]->m_year == -1 ) {
			// add this to the right end point
			//addOff = yoff1;
		}
		
		// correction? for boundary conditions
		int32_t corr = 0;
		// if endpoint of first time interval of tmp2 array
		// is less than edpoint of first time interval of
		// tmp1 array, then skip the first time interval
		// of the tmp2 array. tmp2 might be like "friday"
		// and tmp1 might be "monday" for a date range like
		// "monday - friday" and it turns out that for m_year0
		// friday is on jan 1, so we want to ignore tmp2[0]'s
		// interval which is the time range for that friday
		// in seconds since the epoch. this is kinda an
		// "of by one offset" error.
		//
		// this also fixes "oct 15 - march 15" because it
		// will skip the first oct 15, year0 interval which
		// is an interval of 86400 seconds.
		if ( ni2>0 && ni1>0 && tmp2[0].m_b<tmp1[0].m_b) corr=1;
		// sanity check
		if ( ni2>0 && ni1>0 && 
		     // int2 might have only had one interval which
		     // got "removed" because it was before int1!
		     // so make sure its valid now...
		     // . seems like this is triggered by 
		     //   "last friday of each month from 11pm to saturday 6am"
		     //   by http://hrweb.brevard.k12.fl.us/
		     //   basically many end times to one start time
		     corr < ni2 &&
		     tmp2[corr].m_b < tmp1[0].m_b ) {
			// so because of hrweb.brevard.k12.fl.us just
			// nuke this whole thing rather than core
			return 0;
			// ignore the bogus end time then
			char *xx=NULL;*xx=0; }
		// set this
		bool simple = (di->m_ptrs[0]->m_type & simpleFlags);
		// if no TOD use the simple algo
		if ( ! ( di->m_hasType & DT_TOD ) ) simple = true;
		// reset
		ni3 = 0;
		// get year constraints
		int32_t yoff1 = getYearMonthStart ( m_year0 , 1 );
		int32_t yoff2 = getYearMonthStart ( m_year1 , 1 );
		// if end point intervals are first, then correct that
		if ( corr == 1 ) {
			int3[ni3].m_a = yoff1;
			if ( simple ) int3[ni3].m_b = tmp2[0].m_b;
			else          int3[ni3].m_b = tmp2[0].m_a;
			ni3++;
		}
		// PROBLEM: ni1 is zero because it is before m_year0 but
		//          ni2 is > 0.
		// fixes "2010-11" when m_year0 is 2011 and m_year1 is 2013 for
		// http://www.zvents.com/san-jose-ca/events/show/159288785-in-
		// the-mood-a-1940s-musical-revue
		if ( ni1 == 0 && 
		     ni2 > 0 && 
		     di->m_ptrs[0]->m_type == DT_YEAR &&
		     di->m_ptrs[0]->m_year > 1 &&
		     di->m_ptrs[0]->m_year < m_year0 ) {
			for ( int32_t k = 0 ; k < ni2 ; k++ ) {
				int3[ni3].m_a = yoff1;
				int3[ni3].m_b = int2[k].m_b;
				ni3++;
			}
		}
		// loop over intervals
		for ( int32_t k = 0 ; k < ni1 ; k++ ) {
			int3[ni3].m_a = tmp1[k].m_a;
			// if ran out of intervals in tmp2,use year end
			if ( k+corr >= ni2 ) {
				int3[ni3].m_b = yoff2;
				continue;
			}
			// if a date is a DT_TOD (timeofday) then its
			// interval is like [9pm,midnight] in seconds
			// since the epoch, and we want to use its
			// START point, "9pm" as the endpoint for the 
			// range and not "midnight". Otherwise, we
			// use the actual END point of the range.
			if ( simple) int3[ni3].m_b = tmp2[k+corr].m_b;
			else         int3[ni3].m_b = tmp2[k+corr].m_a;
			// sanity check
			if ( int3[ni3].m_b < int3[ni3].m_a) {
				// this happened for "4pm - 12pm" for
				// http://www.newmexico.org/calendar/events/index.php?com=detail&eID=22948&year=2011&month=01 
				// so let's just give up on such things
				log("dates: bad date intersection for %s",u);
				return 0;
				//char*xx=NULL;*xx=0; }
			}
			ni3++;
		}
		// wrap it up
		return ni3;
	}

	int32_t dcount = 0;
	// scan ptrs if we are a complex date type
	for ( int32_t x = 0 ; x < di->m_numPtrs ; x++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get done
		Date *dx = di->m_ptrs[x];

		// if dx is a season but di already has a month range
		// then ignore it! fixes "Summer Hours: March 15 - Oct. 15"
		// for unm.edu so that is not hurt
		if ( dx->m_type == DT_SEASON &&
		     ( di->m_hasType & DT_RANGE_DAYNUM ||
		       di->m_hasType & DT_MONTH       ||
		       di->m_hasType & DT_DAYNUM ) )
			continue;

		// if we are a range, we must join the points we just
		// added for dx, with the next ptr
		/*
		char hflag2 = 0;
		if ( di->m_type == DT_RANGE && 
		     ((di->m_ptrs[0]->m_type) & simple) &&
		     ((di->m_ptrs[1]->m_type) & simple)  ) {
			// sanity check
			if ( di->m_numPtrs != 2 ) { char *xx=NULL;*xx=0; }
			if ( x == 0 ) hflag2 = MAX_UNBOUNDED;
			else          hflag2 = MIN_UNBOUNDED;
		}
		*/

		// . if int1 is occupied, then put these intervals into "int2"
		// . TODO: union the list intervals together... set 
		//         "add" to true (like "subtract")
		bool swap = ( dcount > 0 ); // && di->m_type != DT_LIST );

		dcount++;

		// sanity check -- these must be empty at this point
		if ( ni2 || ni3 ) { char *xx=NULL; *xx=0; }

		// add in the associated intervals for this complex date
		// into either int1 or int2, if int1 is occupied
		if ( ! swap ) ni1 = addIntervals ( dx, 0, int1 , depth+1,orig);
		else          ni2 = addIntervals ( dx, 0, int2 , depth+1,orig);

		// return -1 on error with g_errno set
		if ( ni1 == -1 ) return -1;
		if ( ni2 == -1 ) return -1;

		// . if this is a weak dow and we have a strong, ignore it
		// . another part of the fix for southgatehouse.com which
		//   has a band called "Sunday Valley" which needs to telescope
		//   to a date with "Friday" in it. so we end up with a 
		//   telescoped date with two different DOWs and this should
		//   resolve them.
		if ( dx->m_type == DT_DOW &&
		     (dx->m_flags & DF_HAS_WEAK_DOW) &&
		     (di->m_flags & DF_HAS_STRONG_DOW) ) {
			// HACK: this means intersection should be full
			if ( ! swap ) {
				ni1 = 1;
				int1[0].m_a = (time_t)0;
				int1[0].m_b = (time_t)0x7fffffff;
			}
			else {
				ni2 = 1;
				int2[0].m_a = (time_t)0;
				int2[0].m_b = (time_t)0x7fffffff;
			}
		}

		// if we do not have int2 occupied, keep chugging so we can
		// get something to intersect
		if ( ! swap ) continue;

		// union int1 and int2 together instead of intersecting?
		bool unionOp = (di->m_type & DT_LIST_ANY);

		// . intersect int1 and int2 and put into int3
		// . returns # of intervals stored into int3
		ni3 = intersect3(int1,int2,int3,ni1,ni2,depth+1,false,unionOp);

		// error? g_errno should be set
		if ( ni3 == -1 ) return -1;

		// "int1" is the accumulator in case we are intersecting
		// more than two sets of intervals
		Interval *tmp;
		tmp  = int1;
		int1 = int3;
		int3 = tmp;
		ni1  = ni3;
		ni3  = 0;
		ni2  = 0;

		// stop if intersection was empty, no need to go further
		if ( ni1 <= 0 ) break;
	}

	// all done if we were a compound, list or range
	if ( di->m_numPtrs > 0 ) {
		// copy results to the requested buffer, but if
		// we are already using that as the accumulator, return now
		if ( int1 == retInt ) return ni1;
		// ok, do the copy
		gbmemcpy ( retInt , int1 , ni1 * sizeof(Interval) );
		// return how many intervals are in "retInt"
		return ni1;
	}

	// int16_tcut
	int32_t num = di->m_num;
	// sanity check
	if ( num < 0 ) { char*xx=NULL;*xx=0; }

	datetype_t dt = di->m_type;

	suppflags_t sfmask = 
		SF_FIRST|
		SF_LAST|
		SF_SECOND|
		SF_THIRD|
		SF_FOURTH|
		SF_FIFTH;
	// int16_tcut
	suppflags_t sflags = di->m_suppFlags;

	// . deal with "first thursday of the month", "second tuesday"
	// . we also use m_supp to indicate presence of "every" or a plural
	//   form of the DOW for our algo that sets DF_BAD_RECURRING_DOW bit
	//   above, so only do this if supp <= 17 now
	if ( dt == DT_DOW && (sflags & sfmask) ) {
		// cycle through the years
		for ( int32_t y = m_year0 ; y < m_year1 ; y++ ) {
		// loop over months
		for ( int32_t m = 1 ; m < 13 ; m++ ) {
			// get year/month start 
			int32_t ym1 = getYearMonthStart ( y , m );
			int32_t ym2 = getYearMonthStart ( y , m+1 );
			// . get dow for the first of that month
			// . dow goes from 1 to 7
			int32_t dow = getDOW ( ym1 + 1 );
			// start it at 1
			int32_t count = 1;
			// reset this
			int32_t lastMatch = -1;
			// count out "sup occruences"
			for ( int32_t d = ym1 ; d < ym2 ; d += 3600*24 , dow++ ) {
				// wrap it
				if ( dow > 7 ) dow = 1;
				// skip if not our day
				if ( dow != num ) continue;
				// are we a match?
				bool match = false;
				// use a mask instead
				if ( count == 1 && (sflags & SF_FIRST) )
					match = true;
				if ( count == 2 && (sflags & SF_SECOND) )
					match = true;
				if ( count == 3 && (sflags & SF_THIRD) )
					match = true;
				if ( count == 4 && (sflags & SF_FOURTH) )
					match = true;
				if ( count == 5 && (sflags & SF_FIFTH) )
					match = true;
				count++;
				// "last monday of the month"?
				if ( sflags & SF_LAST ) {
					lastMatch = d;
					continue;
				}
				// skip if no match
				if ( ! match ) continue;
				// got a match
				int32_t a = d;
				int32_t b = d + 3600*24;
				if ( ! addInterval(a,b,retInt,&ni3,depth)) 
					return -1;
				// that was it!
				//break;
			}
			// the month is over, if had "last monday of the month"
			// we now have to add that...
			if ( lastMatch > 0 ) {
				int32_t a = lastMatch;
				int32_t b = lastMatch + 3600*24;
				if ( ! addInterval(a,b,retInt,&ni3,depth)) 
					return -1;
			}
		} // end month loop
		} // end year loop
	}

	// "wednesday" or "every tuesday"
	else if ( dt == DT_DOW ) {
		// get start of year in seconds since epoch (month=jan=1)
		int32_t yoff1 = getYearMonthStart ( m_year0 , 1 );
		int32_t yoff2 = getYearMonthStart ( m_year1 , 1 );
		// . get dow (day of week) at that time (first day of year)
		// . just do a mod of 24*3600
		// . dow goes from 1 to 7
		int32_t dow = getDOW ( yoff1 + 1 );
		// get first day then
		int32_t diff = num - dow;
		// if before us, catch up
		if ( diff < 0 ) diff += 7;
		// make it into seconds
		int32_t sdiff = diff * 24*3600;
		// to end of 2nd year in seconds since start of 1st year
		//int32_t dend = yoff + 24*3600* 366*2;
		// . step through the next two years, one week a a time
		// . TODO: ignore dows for "Wed nov 17 2009 - thurs dec 3 2009"
		//         type things, since we are bounding by weeks!
		for (  int32_t d = yoff1 ; d < yoff2 ; d += 24*3600*7 ) {
			// adjust min/max for ranges
			int32_t a = d + sdiff;
			int32_t b = a + 3600 * 24;
			//if ( hflag == MIN_UNBOUNDED ) a = d;
			//if ( hflag == MAX_UNBOUNDED ) b = d + 7 * 24*3600;
			// . add interval [a,b) to m_tree1
			// . "yearOff0" is the time_t for start of the year
			// . cycle=week,
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
	}
	// "dec"
	if ( dt == DT_MONTH ) {
		// cycle through the years
		for ( int32_t y = m_year0 ; y < m_year1 ; y++ ) {
			// get year/month start 
			// . "num" is the month and goes from 1-12
			int32_t a = getYearMonthStart ( y , num );
			int32_t b = getYearMonthStart ( y , num + 1 );
			// just paint the middle 10 days or so for 
			// mid-novemeber for villr.com
			if ( di->m_suppFlags & SF_MID ) {
				a += 10*86400;
				b -= 10*86400;
			}
			// do special ranges like "through April"
			//if ( di->m_flags & DF_ONGOING )
			//	a = getYearMonthStart ( y , 1 );
			//if ( hflag == MIN_UNBOUNDED ) a = 0;
			//if ( hflag == MAX_UNBOUNDED ) b = 0x7fffffff;
			// just intersect m_tree1 directly
			if ( ! addInterval (a,b,retInt,&ni3,depth) ) return -1;
		}
	}
	// "12th"
	if ( dt == DT_DAYNUM ) {
		// cycle through the years
		for ( int32_t y = m_year0 ; y < m_year1 ; y++ ) {
		// loop over months
		for ( int32_t m = 1 ; m < 13 ; m++ ) {
			// get year/month start 
			int32_t ym = getYearMonthStart ( y , m );
			// fix for "Mar 31" -- some months may not have
			// that day #, like Feb might only have 28 days...
			if ( num >= 29 ) {
				// get days in month
				int32_t nd = getNumDaysInMonth(m-1,y);
				// skip if overflow
				if ( num > nd ) continue;
			}
			// add the day to it
			int32_t a = ym + (num - 1 )* 24 * 3600;
			int32_t b = ym + (num     )* 24 * 3600;
			// TODO: fix for leap year!!
			//if ( hflag==MIN_UNBOUNDED) a=ym;
			//if( hflag==MAX_UNBOUNDED) b=getYearMonthStart(y,m+1);
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		}
	}
	// "7pm" (every day assumed)
	if ( dt == DT_TOD ) {
		int32_t a = num;
		int32_t b = 24*3600;
		// do special ranges like "through 1pm" or "before 3pm"
		if ( di->m_flags & DF_ONGOING ) {
			a = 0;
			b = num;
			// but if it was like "until 12:30 am" then
			// b - a > 86400 so let's adjust "a" to prevent
			// overlap! this was a problem for
			// http://www.bentfestival.org/
			if ( num > 86400 ) 
				a = num - 86400;
		}
		// if TOD is like 1am, then it is > 24*3600, so we need to
		// fix it here! the custome range intersection logic above
		// will only take the left or right end point of this tod
		// anyway hopefully. i.e. "simple" is false.
		// actually if events starts at 1:30 am assume it lasts for
		// one hour, so ad 3600 seconds to the endpoint.
		// if a is <= b then we assume event lasts until midnight
		// that day, i.e. b = 24*3600.
		// MDW: i made this a >= b because bentfestival.org was
		// starting at "12am" (also i upped 3600 to 3600*3).
		// but we also have to define "night" as going to like 3am.
		if ( a >=b ) 
			b = a + 3600*3;
		// loop over all days in year0 up to and including year1
		int32_t ym0 = getYearMonthStart ( m_year0 , 1 );
		int32_t ym1 = getYearMonthStart ( m_year1 , 1 );

		// fix for "8 [[]] Monday nights" and "9:30 [[]] Monday nights"
		// for salsapower.com
		if ( ! (di->m_suppFlags & SF_HAD_AMPM) &&
		     (orig->m_suppFlags & SF_NIGHT) &&
		     a < 12*3600 ) {
			a += 12*3600;
			// we had "before 6:30" and "evenings" for a date in
			// http://www.restaurantica.com/va/arlington/
			// bonsai-grill/23375901/ (DF_ONGOING was set)
			if ( a > b ) b += 12*3600;
		}

		// . fix for mrmovietimes.com
		// . "10:20am, 5:10" (list of tods, only "am" is given)
		if ( ! (di->m_suppFlags & SF_HAD_AMPM) &&
		     (di->m_suppFlags & SF_PM_BY_LIST) &&
		     a < 12*3600 ) {
			a += 12*3600;
			// see note above (DF_ONGOING was set)
			if ( a > b ) b += 12*3600;
		}


		// sanity check
		//if ( (ym0 % (24*3600) ) != 0 ) { char *xx=NULL;*xx=0;}
		// loop over every day (assume leap year, 366 days)
		for ( int32_t d = ym0 ; d < ym1 ; d += 24*3600 ) {
			int32_t A = a+d;
			int32_t B = b+d;
			//if ( hflag == MIN_UNBOUNDED ) A = 0       + d;
			//if ( hflag == MAX_UNBOUNDED ) B = 24*3600 + d;
			// add it in with yearly offsets in seconds since epoch
			// fasle->DO NOT use dayShift since we are the TOD!
			if ( ! addInterval(A,B,retInt,&ni3,depth,0))return -1;
		}
	}
	// years
	if ( dt == DT_YEAR ) {
		int32_t a = getYearMonthStart ( num     , 1 );
		int32_t b = getYearMonthStart ( num + 1 , 1 );
		//if ( hflag == MIN_UNBOUNDED ) a = 0;
		//if ( hflag == MAX_UNBOUNDED ) b = 0x7fffffff;
		if ( ! addInterval ( a , b,retInt,&ni3,depth ) ) return -1;
	}


	datetype_t specialTypes = 
		DT_HOLIDAY      | // thanksgiving
		DT_SUBDAY       | // mornings
		DT_SUBWEEK      | // weekends
		DT_SUBMONTH     | // last day of month
		DT_EVERY_DAY    | // 7 days a week
		DT_SEASON       | // summers
		DT_ALL_HOLIDAYS ; // "holidays"

	// all done if not holiday
	if ( ! ( dt & specialTypes ) ) return ni3;

	int32_t a;
	int32_t b;
	// scan the years
	for ( int32_t y = m_year0 ; y < m_year1 ; y++ ) {

		// jan 1st, all day
		if ( num == HD_NEW_YEARS_DAY || num == HD_HOLIDAYS ) {
			a = getYearMonthStart ( y,1 );
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// 3rd monday of january, all day
		if ( num == HD_MARTIN_DAY || num == HD_HOLIDAYS ) {
			a = getDOWStart ( y,1,2,3); // Monday=2, get 3rd monday
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// feb 2, all day
		if ( num == HD_GROUNDHOG_DAY ) {
			a = getYearMonthStart (y,2) + 24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// 1st sunday of february
		if ( num == HD_SUPERBOWL ) {
			a = getDOWStart ( y,2,1,1);
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// feb 14
		if ( num == HD_VALENTINES ) {
			a = getYearMonthStart (y,2) + 13*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// feb 15
		if ( num == HD_PRESIDENTS || num == HD_HOLIDAYS ) {
			a = getYearMonthStart (y,2) + 14*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// wednesday before palm sunday
		if ( num == HD_ASH_WEDNESDAY ) {
			a = getDOWStart ( y,4,1,1) - 11*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// mar 17
		if ( num == HD_ST_PATRICKS ) {
			a = getYearMonthStart (y,3) + 16*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// sunday before easter
		if ( num == HD_PALM_SUNDAY ) {
			a = getDOWStart ( y,4,1,1) - 7*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// movable, mar 29
		//if ( num == HD_FIRST_PASSOVER ) {
		//	a = getYearMonthStart (y,3) + 28*24*3600;
		//	b = a + 24*3600;
		//}
		// april 1
		if ( num == HD_APRIL_FOOLS ) {
			a = getYearMonthStart (y,4) ;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// friday before easter
		if ( num == HD_GOOD_FRIDAY ) {
			a = getDOWStart ( y,4,1,1) - 2*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// easter, first sunday in april?
		if ( num == HD_EASTER_SUNDAY  || num == HD_HOLIDAYS ) {
			a = getDOWStart ( y,4,1,1);
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		if ( num == HD_EASTER_MONDAY ) {
			a = getDOWStart ( y,4,1,1) + 24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		//if ( num == HD_LAST_PASSOVER ) {
		//}
		// 3rd monday of april
		if ( num == HD_PATRIOTS_DAY ) {
			a = getDOWStart ( y,4,2,3);
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// wednesday of the last FULL week in april
		if ( num == HD_EARTH_DAY || num == HD_SECRETARY_DAY ) {
			// either the 3rd or 4th wednesday of april
			int32_t a1 = getDOWStart (y,4,4,3);
			int32_t a2 = getDOWStart (y,4,4,4);
			// get start of may
			int32_t a3 = getYearMonthStart(y,5);
			// add thursday+friday+saturday to it
			// and if still in april, it's good!
			if ( a2 + 3*24*3600 < a3 ) a = a2;
			// otherwise, use the 3rd wedsnesday
			else                       a = a1;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// last friday of april (4th or 5th friday)
		if ( num == HD_ARBOR_DAY ) {
			// either the 4th or 5th friday
			int32_t a1 = getDOWStart (y,4,6,4);
			int32_t a2 = getDOWStart (y,4,6,5);
			// get start of may
			int32_t a3 = getYearMonthStart(y,5);
			// and if still in april, it's good!
			if ( a2 < a3 ) a = a2;
			else           a = a1;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// 5th of may
		if ( num == HD_CINCO_DE_MAYO ) {
			a = getYearMonthStart(y,5) + 4*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// 2nd sunday of may
		if ( num == HD_MOTHERS_DAY ) {
			a = getDOWStart ( y,5,1,2);
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// 49 days after easter
		if ( num == HD_PENTECOST_SUN ) {
			a = getDOWStart ( y,4,1,1) + 48*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// last monday of may
		if ( num == HD_MEMORIAL_DAY  || num == HD_HOLIDAYS ) {
			// either the 4th or 5th monday
			int32_t a1 = getDOWStart (y,5,2,4);
			int32_t a2 = getDOWStart (y,5,2,5);
			// get start of june
			int32_t a3 = getYearMonthStart(y,6);
			// and if still in may, it's good!
			if ( a2 < a3 ) a = a2;
			else           a = a1;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// jun 14
		if ( num == HD_FLAG_DAY ) {
			a = getYearMonthStart(y,6) + 13*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// 3rd sunday of june
		if ( num == HD_FATHERS_DAY ) {
			a = getDOWStart ( y,6,1,3);
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// jul 4
		if ( num == HD_INDEPENDENCE  || num == HD_HOLIDAYS ) {
			a = getYearMonthStart(y,7) + 3*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// first monday of sep
		if ( num == HD_LABOR_DAY  || num == HD_HOLIDAYS ) {
			a = getDOWStart ( y,9,2,1);
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// oct 9
		if ( num == HD_LEIF_ERIKSON ) {
			a = getYearMonthStart(y,10) + 8*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// 2nd monday of october
		if ( num == HD_COLUMBUS_DAY ) {
			a = getDOWStart ( y,10,2,2);
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// oct 30
		if ( num == HD_MISCHIEF_NIGHT ) {
			a = getYearMonthStart(y,10) + 29*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// oct 31
		if ( num == HD_HALLOWEEN ) {
			a = getYearMonthStart(y,10) + 30*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// nov 1
		if ( num == HD_ALL_SAINTS ) {
			a = getYearMonthStart(y,11);
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// nov 11
		if ( num == HD_VETERANS  || num == HD_HOLIDAYS ) {
			a = getYearMonthStart(y,11) + 10*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// 4th thursday of nov
		if ( num == HD_THANKSGIVING  || num == HD_HOLIDAYS ) {
			a = getDOWStart ( y,11,5,4);
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// friday after thanksgiving
		if ( num == HD_BLACK_FRIDAY ) {
			a = getDOWStart ( y,11,5,4) + 24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// dec 7
		if ( num == HD_PEARL_HARBOR ) {
			a = getYearMonthStart(y,12) + 6*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// dec 14
		if ( num == HD_ENERGY_CONS ) {
			a = getYearMonthStart(y,12) + 13*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// dec 24
		if ( num == HD_CHRISTMAS_EVE ) {
			a = getYearMonthStart(y,12) + 23*24*3600;
			b = a + 24*3600;
			// usually starts at like 8pm!
			a += 8 * 3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// dec 25
		if ( num == HD_CHRISTMAS_DAY  || num == HD_HOLIDAYS ) {
			a = getYearMonthStart(y,12) + 24*24*3600;
			b = a + 24*3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// dec 31
		if ( num == HD_NEW_YEARS_EVE ) {
			a = getYearMonthStart(y,12) + 30*24*3600;
			b = a + 24*3600;
			// usually starts at like 8pm!
			a += 8 * 3600;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// daily
		if ( num == HD_EVERY_DAY ) {
			// span the whole year
			a = getYearMonthStart(y,1) ;
			b = getYearMonthStart(y+1,1);
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		// weekends
		if ( num == HD_WEEKENDS ) {
			// just use this
			// sunday = 1, saturday = 7
			int32_t mask = 0;
			mask |= 1<<1;
			mask |= 1<<7;
			addIntervalsForDOW ( mask,retInt,&ni3,depth,y );
		}
		if ( num == HD_WEEKDAYS ) {
			int32_t mask = 0;
			mask |= 1<<2;
			mask |= 1<<3;
			mask |= 1<<4;
			mask |= 1<<5;
			mask |= 1<<6;
			addIntervalsForDOW ( mask,retInt,&ni3,depth,y );
		}
		//
		// northern hemisphere definitions
		//
		if ( num == HD_SUMMER ) {
			// june july and august
			a = getYearMonthStart(y,6) ;
			b = getYearMonthStart(y,9) ;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		if ( num == HD_FALL ) {
			a = getYearMonthStart(y,9) ;
			b = getYearMonthStart(y,12) ;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		if ( num == HD_WINTER ) {
			// two pieces here
			a = getYearMonthStart(y,1) ;
			b = getYearMonthStart(y,3) ;
			if ( ! addInterval (a,b,retInt,&ni3,depth) ) return -1;
			a = getYearMonthStart(y,12) ;
			b = getYearMonthStart(y+1,1) ;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		if ( num == HD_SPRING ) {
			a = getYearMonthStart(y,3) ;
			b = getYearMonthStart(y,6) ;
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		if ( num == HD_SCHOOL_YEAR ) {
			// two pieces here
			a = getYearMonthStart(y,1) ;
			b = getYearMonthStart(y,5) ;
			// middle may it ends
			b += 15 * 86400;
			if ( ! addInterval (a,b,retInt,&ni3,depth) ) return -1;
			// starts sep 1
			a = getYearMonthStart(y,9) ;
			b = getYearMonthStart(y+1,1) ; // < jan 1 year+1
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
		if ( num == HD_MORNING ) {
			int32_t ym0 = getYearMonthStart ( y     , 1 );
			int32_t ym1 = getYearMonthStart ( y + 1 , 1 );
			// loop over every day (assume leap year, 366 days)
			for ( int32_t d = ym0 ; d < ym1 ; d += 24*3600 ) {
				// get morning
				int32_t a = d;
				// up until noon
				int32_t b = d + 12*3600;
				// in seconds since epoch
				if ( !addInterval(a,b,retInt,&ni3,depth,false))
					return -1;
			}
		}
		if ( num == HD_AFTERNOON ) {
			int32_t ym0 = getYearMonthStart ( y     , 1 );
			int32_t ym1 = getYearMonthStart ( y + 1 , 1 );
			// loop over every day (assume leap year, 366 days)
			for ( int32_t d = ym0 ; d < ym1 ; d += 24*3600 ) {
				// from noon
				int32_t a = d + 12*3600;
				// up til 6pm
				int32_t b = d + 18*3600; 
				// in seconds since epoch
				if ( !addInterval(a,b,retInt,&ni3,depth,false))
					return -1;
			}
		}
		if ( num == HD_NIGHT ) {
			int32_t ym0 = getYearMonthStart ( y     , 1 );
			int32_t ym1 = getYearMonthStart ( y + 1 , 1 );
			// loop over every day (assume leap year, 366 days)
			for ( int32_t d = ym0 ; d < ym1 ; d += 24*3600 ) {
				// . from 6pm
				// . but that messes up juvaejazz.com
				//   which has evening and 5pm! so make 5pm...
				int32_t a = d + 17*3600;
				// up til midnight -- no! 3am
				// make b be 3am now so "saturday night" and 
				// "until 12:30am" is not empty set
				// fixes www.bentfestival.org
				int32_t b = d + 27*3600;
				//int32_t b = d + 24*3600; 
				// . in seconds since epoch
				// . do not do day shifting on this!
				if ( !addInterval(a,b,retInt,&ni3,depth,false))
					return -1;
			}
		}
		// "last day of the month"
		if ( num == HD_MONTH_LAST_DAY ) {
			// loop over every day (assume leap year, 366 days)
			for ( int32_t m = 1 ; m <= 12 ; m++ ) {
				// get first day of following month
				int32_t ym0 = getYearMonthStart ( y , m+1 );
				// subtract one day to get last day of month
				int32_t a = ym0 - 24*3600;
				// up til midnight
				int32_t b = a + 24*3600; 
				// in seconds since epoch
				if ( ! addInterval(a,b,retInt,&ni3,depth))
					return -1;
			}
		}
		// "first day of the month"
		if ( num == HD_MONTH_FIRST_DAY ) {
			// loop over every day (assume leap year, 366 days)
			for ( int32_t m = 1 ; m <= 12 ; m++ ) {
				// get first day of following month
				int32_t ym0 = getYearMonthStart ( y , m );
				// that is it
				int32_t a = ym0 ;
				// up til midnight
				int32_t b = a + 24*3600; 
				// in seconds since epoch
				if ( ! addInterval(a,b,retInt,&ni3,depth))
					return -1;
			}
		}
		// every month
		if ( num == HD_EVERY_MONTH ) {
			// span the whole year
			a = getYearMonthStart(y,1) ;
			b = getYearMonthStart(y+1,1);
			if ( ! addInterval(a,b,retInt,&ni3,depth)) return -1;
		}
	}		
	return ni3;
}


bool Dates::addIntervalsForDOW ( int32_t      mask   ,
				 Interval *retInt ,
				 int32_t     *ni3    ,
				 int32_t      depth  ,
				 int32_t      year   ) {

	// get start of year in seconds since epoch (month=jan=1)
	int32_t yoff1 = getYearMonthStart ( year     , 1 );
	int32_t yoff2 = getYearMonthStart ( year + 1 , 1 );
	// . get dow (day of week) at that time (first day of year)
	// . just do a mod of 24*3600
	// . dow goes from 1 to 7
	int32_t dow = getDOW ( yoff1 + 1 );
	// back up for first inc in the loop
	dow--;
	// step through the next two years, one day at a time
	for (  int32_t d = yoff1 ; d < yoff2 ; d += 24*3600 ) {
		// inc dow
		if ( ++dow >= 8 ) dow = 1;
		// skip if no match
		if ( ! ( mask & (1<<dow) ) ) continue;
		// adjust min/max for ranges
		int32_t a = d ;//+ sdiff;
		int32_t b = a + 3600 * 24;
		// add interval [a,b) to retInt[] array
		if ( ! addInterval(a,b,retInt,ni3,depth)) return false;
	}
	return true;
}

// . month is from 1 to 13 (jan-dec-jan)
// . returns start of specified month/year in seconds since epoch
time_t getYearMonthStart ( int32_t y , int32_t m ) {
	// hack
	while ( m >= 13 ) { y++; m -= 12; }
	// convert to timestamp in seconds since the epoch
	tm ts;
	// reset
	ts.tm_sec   = 0;
	ts.tm_min   = 0;
	ts.tm_hour  = 0;
	ts.tm_wday  = 0;
	ts.tm_yday  = 0;
	ts.tm_isdst = 0; // daylight savings time?
	// set
	ts.tm_mon  = m - 1; // mktime() expects range of 0-11 for this
	ts.tm_mday = 1;
	ts.tm_year = y - 1900;
	// . TODO: cache this!!!!!
	// . this was returning time local to the server, so fix to UTC
	time_t ttt = mktime(&ts);
	// so jan 1, 2010 returns a ttt that when printed using printTime()
	// prints out "jan 1 7am 2010" so subtract our server timezone
	ttt -= timezone;
	// and now when printed, "ttt" is jan 1, 00:00, the start of the year
	// in GMT/UTC time
	return ttt;
}


time_t getDOWStart ( int32_t y , int32_t m, int32_t dowArg, int32_t count ) {
	// count starts at 1 (first monday of the month, etc.)
	if ( count < 1 ) { char *xx=NULL;*xx=0; }
	if ( count > 5 ) { char *xx=NULL;*xx=0; }
	// sunday=1, saturday=7
	if ( dowArg < 1 ) { char *xx=NULL;*xx=0; }
	if ( dowArg > 7 ) { char *xx=NULL;*xx=0; }

	// start in seconds since epoch for this year month
	int32_t start = getYearMonthStart ( y , m );
	// . what dow is? get delta in seconds
	// . day of epoch is jan 1, 1970, which was a thursday
	//   so add 3 days to make start of sunday
	// . epoch is a time_t of 0
	int32_t delta = start - 3*24*3600;
	// sanity check
	if ( delta < 0 ) { char *xx=NULL;*xx=0; }
	// div by seconds in day to get what day of the week it is
	// for the first of this month on this year
	int32_t dow = (delta / (24*3600)) % 7;
	// add one since dowArg is 1-7
	dow++;
	// now align to our dow
	while ( dow != dowArg ) {
		// inc by one day
		start += 24*3600;
		// wrap it
		if ( ++dow == 8 ) dow = 1;
	}
	// inc by a week for every count over 1
	start += (count-1) * 7*24*3600;
	// that is it
	return start;
}


int32_t getDOW ( time_t t ) {
	struct tm *ts = gmtime ( &t );
	return ts->tm_wday + 1;
}

int32_t getYear ( time_t t ) {
	struct tm *ts = gmtime ( &t );
	return ts->tm_year + 1900;
}

// add the interval to m_int1/m_ni1/m_map1
bool Dates::addInterval ( int32_t a , int32_t b , Interval *int3 , int32_t *ni3 ,
			  int32_t depth , bool useDayShift ) {
	// limit it
	if ( *ni3 >= MAX_INTERVALS ) { char *xx=NULL;*xx=0; }

	// fix "mondays 10pm - 2am"
	if ( useDayShift ) {
		a += m_shiftDay;
		b += m_shiftDay;
	}

	// . no, they can overlap if two holidays fall on the same day
	//   and maybe they do not always do that!
	if ( *ni3 > 0 && 
	     int3[(*ni3)-1].m_a == a &&
	     int3[(*ni3)-1].m_b == b )
		return true;

	// point to it
	Interval *ii = &int3[*ni3];
	// add it to our array of intervals
	ii->m_a = a;
	ii->m_b = b;
	// sanity check
	if ( a > b ) { char *xx=NULL;*xx=0; }

	// maintain order, and can not overlap
	if ( *ni3 > 0 && int3[*ni3-1].m_b > a ) { char *xx=NULL;*xx=0; }

	*ni3 = *ni3 + 1;

#ifdef _DLOG_	
	// log it for debug
	// a depth indicator
	char ds[40];
	for ( int32_t k = 0 ; k < depth ; k++ ) ds[k]='-';
	ds[depth]='\0';
	logf(LOG_DEBUG,"dates: %s [%"INT32",%"INT32")",ds,a,b);
#endif

	return true;
}

// . scan through every interval in "m_int2" (the accumulator)
// . intersect it with each interval in "m_int1" and store the result of
//   each individual intersection into "m_int3"
// . we use a "map" to reduce the number of interval pairs we compare
// . what if they each have overlapping tod intervals, like movie theatre.
// . hmmm... we should not be intersecting two different interval sets of
//   TODs then, cuz they should be in a list or range or something.
// . TODO: add dedup table to prevent same interval from being re-added
// . returns # of intervals stored into int3
// . returns -1 and sets g_errno on error
int32_t Dates::intersect ( Interval *int1 , 
			Interval *int2 ,
			Interval *int3 ,
			int32_t      ni1  ,
			int32_t      ni2  ,
			int32_t      depth ) {


	// then call the new way
	int32_t ni3 = intersect3 (int1,int2,int3,ni1,ni2,depth,false,false);

	/*
	// now we are phasing in merge based intersection and need to
	// make sure it concurs with the hash based intersection
	Interval tmp3[MAX_INTERVALS];
	
	// call the original way
	int32_t tmpni3 = intersect2 (int1,int2,tmp3,ni1,ni2,depth);

	// compare
	if ( tmpni3 != ni3 ) { char *xx=NULL;*xx=0; }

	for ( int32_t x = 0 ; x < ni3 ; x++ ) {
		if ( tmp3[x].m_a != int3[x].m_a ) { char *xx=NULL;*xx=0; }
		if ( tmp3[x].m_b != int3[x].m_b ) { char *xx=NULL;*xx=0; }
	}
	*/

	// return it
	return ni3;
}

int32_t Dates::intersect2 ( Interval *int1 , 
			 Interval *int2 ,
			 Interval *int3 ,
			 int32_t      ni1  ,
			 int32_t      ni2  ,
			 int32_t      depth ) {
	int32_t ni3 = 0;

#ifdef _DLOG_	
	// log it for debug
	// a depth indicator
	char ds[40];
	for ( int32_t k = 0 ; k < depth ; k++ ) ds[k]='-';
	ds[depth]='\0';
	logf(LOG_DEBUG,"dates: %s INTERSECTING",ds);
#endif

	char buf[8];
	int64_t *key = (int64_t *)buf;
	int32_t *A = (int32_t *)&buf[0];
	int32_t *B = (int32_t *)&buf[4];
	char dbuf[10000];
	HashTableX dt;
	dt.set ( 8, 0, 1000 , dbuf,10000, false,m_niceness,"dedupint");

	// make a map of int1 for faster intersecting
	char mapBuf1[10000];
	HashTableX map1;
	map1.set ( 4, 4, 1000 , mapBuf1, 10000, true,m_niceness,"intmap1");
	for ( int32_t i = 0 ; i < ni1 ; i++ ) {
		// int16_tcut
		Interval *ii = &int1[i];
		// get day range
		int32_t d1 =  ii->m_a       / (24*3600);
		int32_t d2 = (ii->m_b - 1 ) / (24*3600);
		// see what intervals touch these days in "int1"
		for ( int32_t d = d1 ; d <= d2 ; d++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// . get who touches day #d
			// . map data ptr pts to our ptr
			if ( ! map1.addKey ( &d , &ii ) ) return -1;
			// debug log for nowe
			//logf(LOG_DEBUG,"map add d=%"INT32"",d);
		}
	}

	// scan all intervals in our accumulator
	for ( int32_t i = 0 ; i < ni2 ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get the interval endpoints
		int32_t a = int2[i].m_a;
		int32_t b = int2[i].m_b;
		// convert into day units
		int32_t d1 = a / (24*3600);
		int32_t d2 = (b-1) / (24*3600);
		// see what intervals in "int1" touch these days
		for ( int32_t d = d1 ; d <= d2 ; d++ ) {
		// get who touches day #d in the int1 intervals
		int32_t slot = map1.getSlot ( &d );
		// chain over those intervals in "int1"
		for ( ; slot >= 0 ; slot = map1.getNextSlot(slot,&d) ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// put the interval into "ii" here
			Interval *ii ;
			ii = *(Interval **)map1.getValueFromSlot(slot);
			// skip if us!
			if ( ii == &int2[i] ) continue;
			// skip if does not intersect [a,b)
			if ( ii->m_b <= a ) continue;
			if ( ii->m_a >= b ) continue;
			// if "ii" contains [a,b), add [a,b) to "int3"
			if ( ii->m_a <= a && ii->m_b >= b ) {
				*A = a;
				*B = b;
			}
			// if we contain "ii"
			else if ( a <= ii->m_a && b >= ii->m_b ) {
				*A = ii->m_a;
				*B = ii->m_b;
			}
			// if we are on the left
			else if ( a <= ii->m_a ) {
				*A = ii->m_a;
				*B = b;
			}
			// if ii is on the left
			else if ( ii->m_a <= a ) {
				*A = a;
				*B = ii->m_b;
			}
			else { char *xx=NULL;*xx=0; }
			// dedup
			if ( dt.isInTable ( key ) ) continue;
			// add it
			if ( ! dt.addKey ( key ) ) return -1;
			// add it
			if ( ! addInterval(*A,*B,int3,&ni3,depth) ) return -1;
			// sanity check
			//char *xx=NULL;*xx=0; 
		}
		}
	}
	return ni3;
}


// if int2 is "negative" (DF_CLOSE_DATE) and int1 is "positive"
// then the merge performs a subtract rather than intersection.
// the sign of int3 will always be positive, unless both int1 and int2
// are negative.
int32_t Dates::intersect3 ( Interval *int1 , 
			 Interval *int2 ,
			 Interval *int3 ,
			 int32_t      ni1  ,
			 int32_t      ni2  ,
			 int32_t      depth ,
			 bool      subtractint2 ,
			 bool      unionOp      ) {
	// int16_tcut
	char *u = ""; if ( m_url ) u = m_url->getUrl();

	// sanity check
	if ( ni1 > MAX_INTERVALS ) { char *xx=NULL;*xx=0; }
	if ( ni2 > MAX_INTERVALS ) { char *xx=NULL;*xx=0; }
	if ( unionOp && subtractint2 ) { char *xx=NULL;*xx=0; }

#ifdef _DLOG_	
	// log it for debug
	// a depth indicator
	char ds[40];
	for ( int32_t k = 0 ; k < depth ; k++ ) ds[k]='-';
	ds[depth]='\0';
	logf(LOG_DEBUG,"dates: %s INTERSECTING",ds);
#endif

	// use ptrs
	Interval *p1 = int1;
	Interval *p2 = int2;
	Interval *p3 = int3;
	Interval *p3max = int3 + MAX_INTERVALS;
	Interval *p1end = int1 + ni1;
	Interval *p2end = int2 + ni2;
	Interval *lastSubInt = NULL;

	goto loop;

 overflow:

	// sometimes we get a huge list of tods like for
	// http://www.ghtransit.com/schedule.html causing ni1 to be 29240
	// and ni2 is already 4386 or so we breack the int3 buf's
	// MAX_INTERVALS. so put checks for that here:
	logf(LOG_DEBUG,"dates: potential overflow for "
	     "%s . returning 0.", u );
	//{ char *xx=NULL;*xx=0;}
	return 0;

	// the merge loop
 loop:

	// stop on overflow
	if ( p3 + 1 > p3max ) {
		goto overflow;
		//return 0;
		//char *xx=NULL;*xx=0;
		//return p3-int3;
	}
	if ( p1 >= p1end ) {
		//gbmemcpy ( p3 , p2 , (p2end - p2) * sizeof(Interval)  );
		//p3 += p2end - p2;
		if ( unionOp ) {
			if ( p3 + (p2end - p2) > p3max ) goto overflow;
			gbmemcpy ( p3 , p2 , (p2end - p2) * sizeof(Interval)  );
			p3 += p2end - p2;
		}
		return p3-int3;
	}

	if ( p2 >= p2end ) {
		// if we were doing a subtraction and not intersection...
		// then the rest of p1 survives
		if ( subtractint2 ) {
			if ( p3 + (p1end - p1) > p3max ) goto overflow;
			gbmemcpy ( p3 , p1 , (p1end - p1) * sizeof(Interval)  );
			p3 += p1end - p1;
		}
		else if ( unionOp ) {
			if ( p3 + (p1end - p1) > p3max ) goto overflow;
			gbmemcpy ( p3 , p1 , (p1end - p1) * sizeof(Interval)  );
			p3 += p1end - p1;
		}
		return p3-int3;
	}

	// discard p1's interval if before p2's interval
	if ( p1->m_b <= p2->m_a ) { 
		// keep it if subtracting though
		if ( subtractint2 ) {
			p3->m_a = p1->m_a;
			p3->m_b = p1->m_b;
			p3++;
		}
		else if ( unionOp ) {
			p3->m_a = p1->m_a;
			p3->m_b = p1->m_b;
			p3++;
		}
		// otherwise, just discard it
		p1++; 
		goto loop; 
	}

	// likewise, for p2
	if ( p2->m_b <= p1->m_a ) { 
		if ( unionOp ) {
			p3->m_a = p2->m_a;
			p3->m_b = p2->m_b;
			p3++;
		}
		p2++; 
		goto loop; 
	}

	// p1  (---------)
	// p2      (------------)
	//
	// p1  (---------)
	// p2  (----------------)
	//
	// p1  (-----------------------)
	// p2      (------) (----)
	//
	// p1  (-----------------------)
	// p2  (------------)
	if ( p1->m_a <= p2->m_a ) {
		// use that a
		p3->m_a = p2->m_a;
		// if doing subtraction
		if ( subtractint2 ) {
			// pick p2's a as our b then
			p3->m_a = p1->m_a;
			p3->m_b = p2->m_a;
			// if the positive interval is huge, do not forget
			// our past subtracted intervals!
			if ( lastSubInt && p3->m_a < lastSubInt->m_b )
				p3->m_a = lastSubInt->m_b;
			// ignore donuts for now
			p1++;
			p3++;
			goto loop;
		}
		else if ( unionOp ) {
			p3->m_a = p1->m_a;
			p3->m_b = p1->m_b;
			p1++;
			p3++;
			goto loop;
		}
		// p1  (---------)
		// p2      (------------)
		//
		// p1  (---------)
		// p2  (----------------)
		if ( p1->m_b <= p2->m_b ) 
			p3->m_b = p1->m_b;
		// p1  (-----------------------)
		// p2      (------------)
		//
		// p1  (-----------------------)
		// p2  (------------)
		else    
			p3->m_b = p2->m_b;
		p3++;
		// which do we inc? p1 or p2?
		Interval *next1 = p1+1;
		Interval *next2 = p2+1;
		// if no next1, we must advance p2 then
		if ( next1 >= p1end ) p2 = next2;
		// if no next2, we must advance p1 then
		else if ( next2 >= p2end ) p1 = next1;
		// pick the one whose next guy's left endpoint is smallest
		else if ( next1->m_a < next2->m_a ) p1 = next1;
		// otherwise, default
		else p2 = next2;
	}
	// p1      (------------)
	// p2  (---------)
	//
	// p1  (----------------)
	// p2  (---------)
	//
	// p1      (------------)
	// p2  (-----------------------)
	//
	// p1  (------------)
	// p2  (-----------------------)
	else {
		// ok now p2->m_a > p1->m_a
		p3->m_a = p1->m_a;
		if ( subtractint2 ) {
			p3->m_a = p2->m_b;
			p3->m_b = p1->m_b;
			// if the positive interval is huge, do not forget
			// our past subtracted intervals!
			if ( lastSubInt && p3->m_a < lastSubInt->m_b )
				p3->m_a = lastSubInt->m_b;
			// if valid, inc p3
			if ( p3->m_a < p3->m_b ) p3++;
			// save this
			lastSubInt = p2;
			// selective inc'ing here!
			if ( p2->m_b <= p1->m_b ) p2++;
			// if endpoints are equal, we inc p1 too!
			if ( p1->m_b <= p2->m_b ) p1++;
			goto loop;
		}
		else if ( unionOp ) {
			p3->m_a = p2->m_a;
			p3->m_b = p2->m_b;
			p2++;
			p3++;
			goto loop;
		}

		// and pick the min b
		if ( p1->m_b <= p2->m_b ) p3->m_b = p1->m_b;
		else                      p3->m_b = p2->m_b;
		// int int3 ptr
		p3++;
		// which do we inc? p1 or p2?
		Interval *next1 = p1+1;
		Interval *next2 = p2+1;
		// if no next1, we must advance p2 then
		if ( next1 >= p1end ) p2 = next2;
		// if no next2, we must advance p1 then
		else if ( next2 >= p2end ) p1 = next1;
		// pick the one whose next guy's left endpoint is smallest
		else if ( next1->m_a < next2->m_a ) p1 = next1;
		// otherwise, default
		else p2 = next2;
	}

	goto loop;
}

/*
bool Dates::printNormalized1 ( SafeBuf *sb ,  Event *ev , int32_t niceness ) { 
	ev->m_date->printNormalized2 ( sb , niceness ,m_words);
	if ( ev->m_numCloseDates <= 0 ) return true;
	if ( ! sb->safePrintf(" closed ") ) return false;
	for ( int32_t i = 0 ; i < ev->m_numCloseDates ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// int16_tcut
		Date *cd = ev->m_closeDates[i];
		// print it
		if ( ! cd->printNormalized2(sb,niceness,m_words)) return false;
	}
	return true;
}

// just print the date itself
bool Date::printNormalized2 ( SafeBuf *sb , int32_t niceness , Words *words ) {
	nodeid_t *tids = words->getTagIds();
	char **wptrs = words->getWords();
	int32_t  *wlens = words->getWordLens ();

	//if ( m_numPtrs == 0 && (m_flags & DF_CLOSE_DATE) )
	//if ( (m_flags & DF_CLOSE_DATE) )
	//	sb->safePrintf(" closed ");

	//if ( m_numPtrs > 0 && m_mostUniqueDatePtr == m_ptrs[0] )
	//	sb->safePrintf("<u>");

	// print out each word
	for ( int32_t j = m_a ; j < m_b ; j++ ) {
		// breathe
		QUICKPOLL(niceness);
		// skip if tag
		if ( tids[j] ) continue;
		// print it otherwise
		sb->safeMemcpy(wptrs[j],wlens[j]);
	}

	//if ( m_numPtrs > 0 && m_mostUniqueDatePtr == m_ptrs[0] )
	//	sb->safePrintf("</u>");

	//if ( m_numPtrs == 0 && (m_flags & DF_CLOSE_DATE) )
	//if ( (m_flags & DF_CLOSE_DATE) )
	//	sb->safePrintf("</font>");

	// telescope ptrs
	for ( int32_t i = 1 ; m_type==DT_TELESCOPE && i<m_numPtrs;i++) {
		// get next ptr
		Date *dp = m_ptrs[i];
		// print delim
		sb->safePrintf(", ");

		//if ( (dp->m_flags & DF_CLOSE_DATE) )
		//	sb->safePrintf(" closed ");

		//if ( dp == m_mostUniqueDatePtr )
		//	sb->safePrintf("<u>");

		// print out each word
		for ( int32_t j = dp->m_a ; j < dp->m_b ; j++ ) {
			// skip if tag
			if ( tids[j] ) continue;
			// print it otherwise
			sb->safeMemcpy(wptrs[j],wlens[j]);
		}

		//if ( dp == m_mostUniqueDatePtr )
		//	sb->safePrintf("</u>");

		//if ( (dp->m_flags & DF_CLOSE_DATE) )
		//	sb->safePrintf("</font>");

	}
	// end in assumed year
	//if ( m_flags & DF_ASSUMED_YEAR ) {
	//	int32_t t1 = m_minPubDate;
	//	// use 90 days instead of 365 since usually people will
	//	// indicate the year if the date is so far out
	//	int32_t t2 = t1 + 90*24*3600;
	//	sb->safePrintf("<font color=blue>");
	//	sb->safePrintf(" ** %s- ",ctime(&t1));
	//	sb->safePrintf("%s",ctime(&t2));
	//	sb->safePrintf("</font>");
	//}
	return true;
}
*/

void resetDateTables ( ) {
	s_mt.reset();
	s_tzt.reset();
	s_dvt.reset();
}

// is di a subdate of us?
bool Date::isSubDate ( Date *di ) {
	// if he has some types we do not have, forget it
	if ( (di->m_hasType & m_hasType) != di->m_hasType ) return false;
	// check each one
	if ( (di->m_hasType & DT_MONTH) && di->m_month != m_month )
		return false;
	if ( (di->m_hasType & DT_DAYNUM) && di->m_dayNum != m_dayNum )
		return false;
	if ( (di->m_hasType & DT_YEAR) && di->m_year != m_year )
		return false;
	// support some tod ranges, like "until 2am"
	//if ( (di->m_hasType & DT_TOD) && di->m_tod != m_tod )
	//	return false;
	if ( di->m_hasType & DT_TOD ) {
		if ( di->m_minTod < m_minTod ) return false;
		if ( di->m_maxTod > m_maxTod ) return false;
	}
	if ( ( di->m_hasType & DT_DOW ) && di->m_dow != m_dow )
		return false;
	return true;
}


void Dates::setEventBrotherBits ( ) {

	char *dom  = m_url->getDomain();
	int32_t  dlen = m_url->getDomainLen();
	if ( m_contentType != CT_XML ) dlen = 0;
	bool isFacebook   = false;
	bool isEventBrite = false;
	bool isStubHub    = false;
	if ( dlen == 12 && strncmp ( dom , "facebook.com" , 12 ) == 0 )
		isFacebook = true;
	if ( dlen == 11 && strncmp ( dom , "stubhub.com" , 11 ) == 0 )
		isStubHub = true;
	if ( dlen == 14 && strncmp ( dom , "eventbrite.com" , 14 ) == 0 )
		isEventBrite = true;
	

	// are implied sections valid? they should be because we need them
	// for santafeplayhouse.org whose event dates span two sentences
	// and are only together in a tight implied section.
	if ( ! m_sections->m_addedImpliedSections ) { char *xx=NULL;*xx=0; }

	////////////////////////
	//
	// set SEC_HASEVENTDOMDOW bit
	//
	// . used for setting SEC_EVENT_BROTHER bits
	//
	///////////////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if was incorporated into a compound date, range or list
		if ( ! di ) continue;
		// do not venture into telescope section
		//if ( di->m_type == DT_TELESCOPE ) break;
		// skip if fuzzy
		//if ( di->m_flags & DF_FUZZY ) continue;
		// skip if pub date
		if ( di->m_flags & DF_PUB_DATE ) continue;
		// skip if registration date
		if ( di->m_flags & DF_REGISTRATION ) continue;
		if ( di->m_flags & DF_NONEVENT_DATE  ) continue;
		if ( di->m_flags5 & DF5_IGNORE ) continue;
		// int16_tcut
		datetype_t dt = (DT_MONTH|DT_DAYNUM);
		bool match = false;
		// need month or dow
		if ( (di->m_hasType & dt) == dt ) match = true;
		if (  di->m_hasType & DT_DOW    ) match = true;
		if ( ! match ) continue;

		// do not venture into telescope section
		//if ( di->m_type == DT_TELESCOPE ) break;
		// if we are a telescope, get the first date in telescope
		if ( di->m_type == DT_TELESCOPE ) {
			// "24 [[]] November 2009 [[]] 8pm - 2am [[]] 
			//  Monday - Saturday" for burtstikilounge.com
			di = di->m_ptrs[0];
			// . only allwo telescoped daynum i guess for
			//   burtstikilounge.com
			// . otherwise telescoped tods for folkmads.org are
			//   getting SEC_HASEVENTDOMDOW set!
			if ( di->m_hasType != DT_DAYNUM ) continue;
		}
		// telescopes are not fuzzy
		else {
			// skip if fuzzy
			if ( di->m_flags & DF_FUZZY ) continue;
		}

		// . telescope it all up
		// . use compound section now in case date spans multiple
		//   sentence sections
		Section *sd = di->m_compoundSection;
		// telescope all the way up
		for ( ; sd ; sd = sd->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// stop if already set
			if ( sd->m_flags & SEC_HASEVENTDOMDOW ) break;
			// mark it
			sd->m_flags |= SEC_HASEVENTDOMDOW;
		}
	}

	// sanity
	if ( m_sections->m_lastSection->m_next ) { char *xx=NULL;*xx=0; }
	if ( ! s_init42 ) { char *xx=NULL;*xx=0; } // h_details, h_more
	// 
	// similar to above, we have issues with the last link like
	// "(more)" is being combined into the previous sentence! so
	// also get the hash of the last link in the section
	//
	for ( Section *si = m_sections->m_lastSection; si ; si = si->m_prev ) {
		// breathe
		QUICKPOLL(m_niceness);
		// must be a href tag, a link!
		if ( si->m_tagId != TAG_A ) continue;
		// get the hash. it is not in the <a> section so
		// we have to scan
		int32_t kmax = si->m_a + 20;
		if ( kmax > m_nw ) kmax = m_nw;
		if ( kmax > si->m_b ) kmax = si->m_b;
		uint32_t tagh = 0;
		bool gotOne = false;
		for ( int32_t k = si->m_a ; k < kmax ; k++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not alnum
			if ( ! m_wids[k] ) continue;
			// must be "details" or "more"
			if ( m_wids[k] == h_details ) gotOne = true;
			if ( m_wids[k] == h_more    ) gotOne = true;
			// hash it up
			tagh ^= m_wids[k];
			tagh <<= 1;
			if ( tagh == 0 ) tagh = 1234567;
		}
		// need "more" or "details" in link
		if ( ! gotOne ) continue;
		// if no text, forget it... what about an image???
		if ( ! tagh ) continue;
		// set last sent content hash
		Section *sp = si;
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// stop if already set, no need to telescope up more
			if ( sp->m_lastLinkContentHash32 ) break;
			// set it
			sp->m_lastLinkContentHash32 = tagh;
		}
	}	


	// make hashtable of generic words for determining if the event
	// brothers are actually just a list of dates
	static char *s_gwords [] = {
		"open",
		"opens",
		"closed",
		"closes",
		"on",
		"for",
		"hours",
		"of",
		"operation",
		"store",
		"business",
		"office"
	};
	// store these words into table
	static HashTableX s_gt;
	static char s_gtbuf[2000];
	static bool s_init3 = false;
	if ( ! s_init3 ) {
		s_init3 = true;
		s_gt.set(8,0,128,s_gtbuf,2000,false,m_niceness,"gttab");
		int32_t n = (int32_t)sizeof(s_gwords)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// set words
			char *s = s_gwords[i];
			int64_t h = hash64n(s);
			s_gt.addKey ( &h );
		}
	}

	// int16_tcut
	wbit_t *bb = m_bits->m_bits;
	// scan the sections
	for ( Section *si = m_sections->m_rootSection ; si ; si = si->m_next) {
		// breathe
		QUICKPOLL(m_niceness);
		// must be in a brother list
		if ( ! si->m_nextBrother ) continue;
		// and first in the list
		if ( si->m_prevBrother ) continue;
		// only look for "hard" brothers
		// . fixes trumba so "re-opens saturday at 9am" can telescope
		//   to the timepoint date range a few sentences above it.
		// . fixes publicbroadcasting so soul power's november
		//   date range can be telescoped to
		// . fixes blackbirdbuvette.com so store hours, kitchen hours
		//   are not date brothers
		// . fixes collectorsguide.com so "every sunday before 1pm"
		//   can telescope to the store hours in the same hard section
		// . ...
		if ( ! m_sections->isHardSection ( si ) ) continue;
		// count bullet delim as soft for this to fix
		// blackbirdbuvette.com whose store hours and kitchen hours
		// are in a bullet delimeted list
		if ( si->m_baseHash == BH_BULLET ) continue;
		// . get first one we find
		// . this algo hurts sybarite5.org because one of the sections
		//   at the top is an event and not a header, and it has
		//   a monthday range that ends up being a header for all
		//   the event sections below which is really bad! this was
		//   originally a fix for publicbroadcasting soul power but
		//   we fixed that by not doing this for soft sections!
		// . NO! actually that is a header in sybarite5.org! wow...
		// . and we need this to fix adobetheater.org too for which
		//   we otherwise lose a range header
		Section *first = NULL;
		// are the brothers really part of the same set of store hours?
		// date brothers?
		bool eventBrothers = false;
		Section *bro = si;
		Section *last = NULL;
		bool diffTODs = false;
		bool diffDays = false;
		int32_t px1;
		int32_t rx1;
		int32_t ex1;
		int32_t tx1;
		int32_t dx1;
		int32_t ax1;
		int32_t lh1;
		// scan the brother list
		for ( ; bro ; bro = bro->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// . if its a registration section, disregard it
			// . fixes signmeup.com which has a differing phone
			//   # and dates in a registration section. that
			//   cause all brothers to be event brothers and the
			//   race tod start times could not telescope to the
			//   brother that had the day, "Thanksgiving"
			if ( bro->m_flags & SEC_HAS_REGISTRATION ) continue;
			// . TODO: include subfields
			// . TODO: storehours detection
			// . TODO: adjacent date/price/email detection
			// skip if has nothing special
			int32_t px2 = bro->m_phoneXor;
			int32_t rx2 = bro->m_priceXor;
			int32_t ex2 = bro->m_emailXor;
			int32_t ax2 = bro->m_addrXor;
			// should help fix pagraphs that repeat the same date
			// like denver.org single's day soiree
			int32_t tx2 = bro->m_todXor;
			int32_t dx2 = bro->m_dayXor;
			int32_t lh2 = bro->m_lastLinkContentHash32;
			// cancel date xor if not in tod and dom/dow section
			//if ( ! (bro->m_flags & SEC_HAS_TOD) ) 
			//	tx2 = 0;
			//if ( ! (bro->m_flags & SEC_HAS_DOM) &&
			//     ! (bro->m_flags & SEC_HAS_DOW) ) 
			//	tx2 = 0;
			if ( ! (bro->m_flags & SEC_HASEVENTDOMDOW) )
				tx2 = 0;
			// skip if nothing to compare to prev bro with
			if ( ! px2 && ! rx2 && ! ex2 && ! tx2 && ! dx2 &&
			     ! lh2 )
				continue;
			// if no last, skip then
			if ( ! last ) {
			update:
				last = bro;
				px1 = px2;
				rx1 = rx2;
				ex1 = ex2;
				tx1 = tx2;
				dx1 = dx2;
				ax1 = ax2;
				lh1 = lh2;
				continue;
			}

			// are both brothers sentences?
			//bool bothSentences = 
			//	( (bro ->m_flags & SEC_SENTENCE) &&
			//	  (last->m_flags & SEC_SENTENCE) );

			// . get our phone # xor
			// . is last bro a different phone # than us?
			// . this messes up cabq.gov libraries because
			//   in one section they'd have the primary phone
			//   and then a list of secondary phone #'s in the
			//   section after it
			// . but without it many more urls mess up in a 
			//   worse way, but getting description not theirs
			if ( px1 && px2 && px1 != px2 )
				eventBrothers = true;

			// . need hasprice set. can't be single sentences.
			// . they often list pricing info per sentence.
			// . fixes collectorsguide.com which has
			//   "Admission ... adults $7, children/seniors $3"
			//   "Combo  ... adults $12; children/seniors $5"
			//   as two separate sentences next to each other.
			// . we seem to have multiple sections in one event
			//   that have ticket prices... denver.org
			//   melodytent, collectorsguide, so take this out
			//   for now
			//if ( rx1 && rx2 && rx1 != rx2 && ! bothSentences )
			//	eventBrothers = true;

			// differing emails?
			if ( ex1 && ex2 && ex1 != ex2 ) 
				eventBrothers = true;
			// differing TOD dates?
			if ( tx1 && tx2 && tx1 != tx2 ) {
				diffTODs      = true;
				eventBrothers = true;
			}
			// . differing day dates? burtstikilounge.com calendars
			// . TOD is telescoped to by the day, like if its
			//   store hours or something...
			if ( dx1 && dx2 && dx1 != dx2 ) {
				diffDays      = true;
				eventBrothers = true;
			}
			// . same tod, different address?
			// . removes Tingley Beach description from the
			//   rio grande zoo because eventbroters was not
			//   getting set because they all had the same hours
			//   and price, etc.
			if ( tx1 && tx2 && ax1 && ax2 && ax1 != ax2 )
				eventBrothers = true;
			// . subfields... like "cost:"
			// . see isCompatible2() for this code
			//if ( getNumSubfieldsInCommon ( last, bro ) > 0 )
			//	sameSubFields = true;

			// . last link content hash the same? (more) (details)
			// . sometimes it is merged into last sentence!
			if ( lh1 && lh2 && lh1 == lh2 )
				eventBrothers = true;

			// do not do the event brothers algo for specific
			// xml feeds because we ignore all but one date
			// for these guys. we already know the event 
			// delimeters. and this often will find a phone or
			// email in the <description> and another one in a
			// brother section even though the xml is well defined.
			// because eventbrite has an <organizer> and <tickets>
			// section with their own independent contact info.
			if ( isEventBrite ||
			     isFacebook   ||
			     isStubHub )
				eventBrothers = false;

			// if no eventbrothers yet, keep going
			if ( ! eventBrothers ) goto update;

			// remember the first one. we start setting the bit
			// at the first one to be recognized as an event 
			// because it is often the case there are headers
			// above the list of event sections.
			if ( ! first ) first = last;

			Section *s1 = last;
			Section *s2 = bro ;

			// point s1/s2 to first sentence in those sections, if
			// not already
			if ( ! ( s1->m_flags & SEC_SENTENCE ) )
				s1 = s1->m_nextSent;
			if ( ! ( s2->m_flags & SEC_SENTENCE ) )
				s2 = s2->m_nextSent;

			// or if not in sentences... wtf? this happens when
			// indexing a script page or something
			// fixes core for http://www.neaq.org/Scripts/feed/feed2js.php?src=http%3A%2F%2Fwww.eventkeeper.com%2Fekfeed%2FNEAQ_aq_homefeed2.xml&num=2&tz=-2&utf=y&html=y
			if ( ! s1 || ! s2 ) break;

			// must be adjacent. otherwise they are event brothers.
			if ( s1->m_nextSent != s2 ) break;

			// if they are two pure dates, then its a list of dates
			// and they are not *event* brothers. fixes stuff
			// like "Mon-Fri 9-5" and "Sat 9-3" so they are not
			// event brothers.
			int32_t a,b;
			bool pure1 = true;
			bool pure2 = true;
			a = s1->m_a;
			b = s1->m_b;
			for ( int32_t i = a ; i < b ; i++ ) {
				// breathe
				QUICKPOLL(m_niceness);
				// skip if not wid
				if ( ! m_wids[i] ) continue;
				// if part of date, skip it
				if ( bb[i] & D_IS_IN_DATE ) continue;
				// if generic word, skip it
				if ( s_gt.isInTable(&m_wids[i]) ) continue;
				// skip if number, like ticket price
				if ( m_words->isNum(i) ) continue;
				// crap, not pure
				pure1 = false;
				break;
			}
			// stop if not pure, no point in doing more
			if ( ! pure1 ) break;
			// otherwise, check the purity of the next sentence
			a = s2->m_a;
			b = s2->m_b;
			// see if 2nd brother is all pure words too
			for ( int32_t i = a ; i < b ; i++ ) {
				// breathe
				QUICKPOLL(m_niceness);
				// skip if not wid
				if ( ! m_wids[i] ) continue;
				// if part of date, skip it
				if ( bb[i] & D_IS_IN_DATE ) continue;
				// if generic word, skip it
				if ( s_gt.isInTable(&m_wids[i]) ) continue;
				// crap, not pure
				pure2 = false;
				break;
			}
			// stop if not pure
			if ( ! pure2 ) break;
			// if both pure, not event brother
			eventBrothers = false;
			first         = NULL;
			goto update;
		}

		// bail if not EVENT brothers
		if ( ! eventBrothers ) continue;

		// . scan all event brothers
		// . if we have a single store hours container and only
		//   that has the address, assume it is the store hours
		//   for all event brothers
		Section *shc = NULL;
		for ( Section *bro = first; bro ; bro = bro->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not store hours
			if (!(bro->m_flags & SEC_STOREHOURSCONTAINER))continue;
			// if we already have one, that's no good
			if ( shc ) { shc = NULL; break; }
			// set it
			shc = bro;
		}
		// must have address for all brothers
		if ( shc && shc->m_addrXor == 0 ) shc = NULL;
		// now if we had a single store hours container
		for ( Section *bro=first; shc && bro; bro=bro->m_nextBrother ){
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not store hours
			if ( bro->m_addrXor == 0 ) continue;
			// must match
			if ( bro->m_addrXor == shc->m_addrXor ) continue;
			// not good otherwise
			shc = NULL;
			break;
		}

		// . crap for terrence wilson there are 3 sections:
		// . <div>date1</div><div>date2s</div><div>desc+addr</div>
		// . i was thinking that if the section contains the address
		//   of an event, the event can use the description, so
		//   maybe carve out this exception in the event desc algo
		//   in Events.cpp

		// otherwise, set bit on all brothers
		for ( Section *bro = first; bro ; bro = bro->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// a legit store hours section is immune since it is
			// not an event really. this way the dates in there
			// can telescope out to join with the dates in the
			// true event brothers. should fix burtstikilounge.com.
			if ( bro == shc ) continue;
			// this will prevent dates in "bro" from being
			// headers outside this section as well as event
			// description sharing between event brother sections.
			bro->m_flags |= SEC_EVENT_BROTHER;
		}
	}
}

// . find a dow based date (no single month daynum )
// . get date after it if dow based as well
// . if no such date after it, evaluate it by itself then
// . otherwise: get section containing both
// . evaluate all dates in that section, must all be legit schedule dates
void Dates::setStoreHours ( bool telescopesOnly ) {

	// int16_tcut
	//wbit_t *bb = m_bits->m_bits;

	datetype_t specialTypes = 
		DT_HOLIDAY      | // thanksgiving
		DT_SUBDAY       | // mornings
		DT_SUBWEEK      | // weekends
		DT_SUBMONTH     | // last day of month
		DT_EVERY_DAY    | // 7 days a week
		DT_SEASON       | // summers
		DT_ALL_HOLIDAYS ; // "holidays"


	// int16_tcut
	wbit_t *bb = m_bits->m_bits;

	// detect words before this date like "hours:" or "open:"
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if not telescope
		if ( telescopesOnly && di->m_type != DT_TELESCOPE ) continue;
		// need tod though. all event times have tods so this
		// should just speed things up.
		if ( ! (di->m_hasType & DT_TOD) ) continue;
		// official dates are exempt (i.e. facebook start_date)
		if ( di->m_flags & DF_OFFICIAL ) continue;
		/*
		// it can be closure not too, not just store hours...
		// like for asthmaallies.org: "Holiday Office Closures:
		// December 22 - 26, 2011"
		if ( ! (di->m_hasType & DT_DOW) ) continue;
		// 2+ dows required to fix "Fri, Nov 27, 8:00p"
		int32_t numDow = getNumBitsOn8(di->m_dowBits);
		if ( numDow <= 1 ) continue;
		// . now month unless in a month range or monthdaynum range
		// . if month, it needs like Nov x - y or Nov x - Dec y
		if ( (di->m_hasType & DT_MONTH) &&
		     !(di->m_hasType & DT_RANGE_MONTHDAY) )
			continue;
		*/
		// scan the words before each date element in "di"
		int32_t ne;
		Date **de = getDateElements(di,&ne);
		// starts with a dow like "Monday-Friday.."
		// no. okstate.edu starts with "Aug 22 - Dec 16 2011"
		// and then lists the dow hours. and at top it has
		// "Fall Semester". 
		// ok, then, we don't want to hit "doors open: 8pm" on
		// reverbnation.com so try this one:
		//if ( de[0]->m_type == DT_TOD ) continue;
		// fixes "Fall Hours * Aug 24 - Nov 26 * M-Th 7:30am-...
		// for tarlton.law.utexas.edu
		//if ( (de[0]->m_hasType & DT_SEASON) &&
		//     m_wids[de[0]->m_b-1] == h_hours )
		//	goto gotOne;
		// so scan each date element then
		int32_t x; for ( x = 0 ; x < ne ; x++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get it
			Date *dx = de[x];
			// scan for words before
			int32_t a = dx->m_a;
			int32_t min = a - 30;
			if ( min < 0 ) min = 0;
			int32_t alnumCount = 0;
			int64_t lastWid = 0LL;
			for ( ; a >= min ; a-- ) {
				// breathe
				QUICKPOLL(m_niceness);
				// skip tags and punct
				if ( ! m_wids[a] ) continue;
				// check it. often "hours" is in the date
				// like it is for utexas.edu "Fall Hours"
				if ( m_wids[a] == h_hours ) 
					break;
				// do not count dates in list towards 
				// alnumcount
				if ( bb[a] & D_IS_IN_DATE ) continue;
				// limit alpha count
				if ( ++alnumCount >= 7 ) 
					break;
				if ( (m_wids[a] == h_is ||
				      m_wids[a] == h_be ||
				      m_wids[a] == h_are ) &&
				     lastWid == h_open  ) 
					break;
				// if open is first word in sentence? kinda..
				if ( m_wids[a] == h_open && a-2<min ) 
					break;
				// fall semester: m-f ... okstate.edu
				if ( m_wids[a] == h_semester ) 
					break;
				// fron office
				if ( m_wids[a] == h_office ) 
					break;
				// box office: 1-3pm... kinda like
				// office hours.
				if ( lastWid == h_office &&
				     m_wids[a] == h_box )
					break;
				// reception desk
				if ( lastWid == h_desk &&
				     m_wids[a] == h_reception )
					break;
				// update this
				lastWid = m_wids[a];
			}
			// skip this date element if not store hours b4 it
			if ( alnumCount >= 7 || a < min ) continue;
			// ok, got one
			break;
		}
		// if we had no luck... try next date, "di"
		if ( x >= ne ) continue;
		// otherwise, it was store hours...
		//	gotOne:
		di->m_flags |= DF_STORE_HOURS;
		di->m_flags |= DF_WEEKLY_SCHEDULE;
		//
		// set SEC_STOREHOURSCONTAINER
		//
		Section *sp = di->m_section;
		// initial dates xor in "sd"
		int32_t todXor = sp->m_todXor;
		// can't be zero - we contain the store hours
		if ( ! todXor ) continue;//{ char *xx=NULL;*xx=0; }
		// keep setting up as int32_t as datexor remains unchanged
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if a date was gained
			if ( sp->m_todXor != todXor ) break;
			// set otherwise
			sp->m_flags |= SEC_STOREHOURSCONTAINER;
		}
	}

	
	// scan all dates we got
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if not in body (i.e. from url)
		if ( ! (di->m_flags & DF_FROM_BODY) ) continue;
		// ignore if fuzzy
		if ( di->m_flags & DF_FUZZY ) continue;
		// skip if registration date, not considered store hours
		if ( di->m_flags & DF_REGISTRATION ) continue;
		// skip if not telescope
		if ( telescopesOnly && di->m_type != DT_TELESCOPE ) continue;
		// turn it off in case we are a re-call on a telescope
		//di->m_flags &= ~DF_SCHEDULECAND;
		// must have a dow/tod type thing
		if ( ! ( di->m_hasType & (specialTypes|DT_DOW|DT_TOD) ) ) 
			continue;
		// no month day
		if ( (di->m_hasType & DT_DAYNUM) )
		     // no more exceptions!
		     // exception: Nov 23 - Dec 5
		     //!(di->m_hasType & DT_RANGE_MONTHDAY) &&
		     // exception: Nov 23-27
		     //!(di->m_hasType & DT_RANGE_DAYNUM) &&
		     // exception: Nov - Dec
		     //di->m_month != -1 )
			continue;
		// or month
		if ( (di->m_hasType & DT_MONTH ) )
			continue;
		// set this bit
		di->m_flags |= DF_SCHEDULECAND;
	}


	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// must be set
		if ( ! ( di->m_flags & DF_SCHEDULECAND ) ) continue;
		// skip if not telescope
		if ( telescopesOnly && di->m_type != DT_TELESCOPE ) continue;

		// get date after us
		Date *dj = NULL;
		// scan for it
		for ( int32_t j = i + 1 ; j < m_numDatePtrs ; j++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// get it
			dj = m_datePtrs[j];
			// stop if got it
			if ( dj ) break;
		}

		// if not schedule-y null it out
		if ( dj && !(dj->m_flags & DF_SCHEDULECAND) ) dj = NULL;

		/*
		// or if too many words in between di and dj, they are not
		// part of the same store hours then
		int32_t acount = 0;
		int32_t tcount = 0;
		for ( int32_t r = di->m_b ; dj && r < dj->m_a ; r++ ) {
			// do not scan too far
			if ( ++tcount >= 20 ) break;
			// count alnums
			if ( m_wids[r] && ++acount >= 6 ) break;
		}
		// stop if too far apart, do not try to pair up
		if ( tcount >= 20 ) dj = NULL;
		if ( acount >=  6 ) dj = NULL;
		*/

		// if had a possible partner get section containing both
		Section *sd = di->m_section;
		// blow it up
		for ( ; dj && sd ; sd = sd->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// test
			if ( sd->contains2 ( dj->m_a ) ) break;
		}

	redo:
		
		// all dates in section must be legit now too
		int32_t       fd = sd->m_firstDate - 1;
		// craziness! if there are two sentences "...before. "
		// "The spring..." the date can span those two sentences
		// "before. The spring" and cause fd to be -1. like for
		// thealcoholenthusiast.com...
		if ( fd < 0 ) { char *xx=NULL;*xx=0; }
		// assume good
		bool       good = true;
		// is it an open-ended tod range like "before 1pm"?
		bool       openRange = true;
		datetype_t acc = 0;
		char       dowBits = 0;
		int32_t       numSecondsOpen = 0;
		int32_t       hadNoEndTime = 0;
		bool isStoreHours = false;
		Date      *last = NULL;
		int32_t       lastNumDow = 0;
		int32_t kmax = m_numDatePtrs;

		// di might be a telescope so we need this
		if ( di->m_type == DT_TELESCOPE ) {
			// make dj NULL
			good = false;
			// do not do loop below now
			kmax = 0;
		}

		// scan those
		for ( int32_t k = fd ; k < kmax ; k++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// test
			Date *dk = m_datePtrs[k];
			if ( ! dk ) continue;
			// stop on breach
			if ( dk->m_a >= sd->m_b ) break;
			// skip if not sure if a date
			if ( dk->m_flags & DF_FUZZY ) continue;
			// skip if registration date, not considered store hrs
			if ( dk->m_flags & DF_REGISTRATION ) continue;
			// fix "irishtimes.com's latest news service is updated
			// constantly from 6.30 a.m. until 10 p.m. daily."
			// from coring because its todxor was 0!
			if ( dk->m_flags & DF_PUB_DATE ) continue;
			// this causes the ! todXor core dump below because
			// we do not set todxor for ticket/registration dates
			// even if they are indeed store hours
			if ( dk->m_flags & DF_NONEVENT_DATE ) continue;
			if ( dk->m_flags5 & DF5_IGNORE ) continue;
			// stop if date is not store hours-ish
			if ( ! (dk->m_flags & DF_SCHEDULECAND) ) {
				good = false;
				break;
			}

			// . count words between this date and last one
			// . if too many then use the smallest section
			//   around date "di"
			// . should fix abqtango.com ...
			if ( last && dj ) {
				// reset counters
				int32_t acount = 0;
				int32_t tcount = 0;
				int32_t r = last->m_b;
				for ( ; r < dk->m_a ; r++ ) {
					// do not scan too far
					if ( ++tcount >= 20 ) break;
					// count alnums
					if ( m_wids[r] && ++acount >= 6) break;
				}
				// stop if too far apart, do not try to pair up
				if ( r < dk->m_a ) {
					// use smallest section around "di"
					sd = di->m_section;
					// and just use "di", no pairing
					// up with another date
					dj = NULL;
					goto redo;
				}
			}
			// assign this
			last = dk;

			// otherwise, accumulate bits
			acc |= dk->m_hasType;
			// and days of the week as well
			dowBits |= dk->m_dowBits;
			// how many days of the week are mentioned
			int32_t numDow = getNumBitsOn8(dk->m_dowBits);
			// fix "Wednesday - Saturday [[]] 10:00am - 6:00pm"
			// for woodencow.com url
			if ( numDow <= 0 ) numDow = lastNumDow;
			// assume at least 1 to fix maret.org since we
			// do not have telscopes yet
			if ( numDow <= 0 ) numDow = 1;
			// for next guy
			lastNumDow = numDow;
			// need tod date at this point
			if ( ! ( dk->m_hasType & DT_TOD ) ) continue;
			// if not range, no end time then
			if ( ! (dk->m_hasType & DT_RANGE_TOD) ) {
				// count it
				hadNoEndTime++;
				// and get next date
				continue;
			}
			// and total hours open per week
			int32_t min = dk->m_minTod;
			int32_t max = dk->m_maxTod;
			if ( min < 0   ) continue;
			if ( min > max ) { char *xx=NULL;*xx=0; }
			// this will be zero if not a tod range
			numSecondsOpen += numDow * (max - min);
			// disallow "Every Sunday before 1pm" by 
			// itself from collectorsguide.com
			if ( ! (dk->m_flags & DF_ONGOING ) ) openRange = false;
		}

		// if had some incomaptible dates, then just try di by itself
		if ( ! good ) dj = NULL;

		// . if no good partner date, just try di by itself
		// . we come here right away for telescopes only
		if ( ! good ) {
			// if has month/daynum in it, forget it
			if ( ! (di->m_flags & DF_SCHEDULECAND) )
				continue;
			// reset crap
			numSecondsOpen = 0;
			openRange      = true;
			hadNoEndTime   = 0;
			// do not do section telescoping thing now
			kmax = 0;
			// otherwise init these to di's stuff
			acc     = di->m_hasType;
			dowBits = di->m_dowBits;
			if ( (di->m_hasType & DT_RANGE_TOD) &&
			     ! (di->m_flags & DF_ONGOING ) ) 
				openRange = false;
			// how many days of the week are mentioned
			int32_t numDow = getNumBitsOn8(dowBits);
			// and total hours open per week
			int32_t min = di->m_minTod;
			int32_t max = di->m_maxTod;
			// this will be zero if not a tod range
			if ( min >= 0 && max > min )
				numSecondsOpen += numDow * (max - min);
		}

		// "seven days a week" are like dows
		if ( acc & 
		     ( DT_HOLIDAY      | // thanksgiving
		       DT_SUBDAY       | // mornings
		       DT_SUBWEEK      | // weekends
		       DT_EVERY_DAY    | // 7 days a week
		       DT_ALL_HOLIDAYS ))// "holidays"
			// treat it like a dow for these purposes
			acc |= DT_DOW;

		// . must have dow and tod range to be store hours
		// . this algo fixes burtstikilounge.com which has a
		//   "6pm-2am [[]] Monday - Saturday" type of date which is
		//   a telescope, and we have no telescopes here yet
		if ( ! (acc & DT_RANGE_TOD) ) continue;
		if ( ! (acc & DT_DOW      ) ) continue;

		// need at least one closed tod range
		if ( openRange ) continue;

		// get # dow it is on
		int32_t numDow = getNumBitsOn8 ( dowBits );
		// . must be open at least 4 days a week
		// . no, let's make it 2+ for a weekly schedule so that
		//   date like "11:30 sat and sun only" on unm.edu are
		//   "event candidates" in Events.cpp. (recurring dow implied)
		if ( numDow < 2 ) continue;
		// its part of a weekly schedule now at least
		dateflags_t df = DF_WEEKLY_SCHEDULE;

		// . set weekly schedule section
		// . dates in the same weekly schedule section do not
		//   cause the mult events penalty in Events.cpp
		//di->m_storeHoursSection = lastsd;
		// . and for at least 28 hours a week
		// . but fix unm.edu which has things like "8am Mon - Fri"
		//   so that they are still store hours.
		if ( numSecondsOpen >= 28*3600 ) isStoreHours = true;
		// if we had like "mon-fri 9am"... assume store hours
		if ( hadNoEndTime > 0 ) isStoreHours = true;

		// skip if not store hours
		if ( isStoreHours ) df |= DF_STORE_HOURS;

		// if we are a telescope or an individual date set the flag
		if ( ! good )
			di->m_flags |= df;

		// mark all in our section now (only if not telescope)
		for ( int32_t k = fd ; k < kmax ; k++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// test
			Date *dk = m_datePtrs[k];
			if ( ! dk ) continue;
			// stop on breach
			if ( dk->m_a >= sd->m_b ) break;
			// skip if not sure if a date
			if ( ! (dk->m_flags & DF_SCHEDULECAND ) ) continue;
			// mark it
			dk->m_flags |= df;
		}

		// bail if not store hours
		if ( ! isStoreHours ) continue;

		//
		//
		// set SEC_STOREHOURSCONTAINER
		//
		//

		// this makes telescopes core because one piece of them
		// might not have the tod or todXor set
		if ( ! good ) continue;

		Section *sp = sd;
		// initial dates xor in "sd"
		int32_t todXor = sd->m_todXor;
		// can't be zero - we contain the store hours
		if ( ! todXor ) { char *xx=NULL;*xx=0; }
		// keep setting up as int32_t as datexor remains unchanged
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if a date was gained
			if ( sp->m_todXor != todXor ) break;
			// set otherwise
			sp->m_flags |= SEC_STOREHOURSCONTAINER;
		}
	}


	/////////////////////////////
	//
	// set DF_KITCHEN_HOURS
	// 
	// . this means it is not quite store hours
	// . could be happy hour tod range, kitchen hours, etc.
	// . this does not need a tod range like store hours since it can
	//   have something like "kitchen hours: until 10 pm" and needs to
	//   telescope to the store hours
	//
	/////////////////////////////
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if not in body (i.e. from url)
		if ( ! (di->m_flags & DF_FROM_BODY) ) continue;
		// skip if not telescope
		if ( telescopesOnly && di->m_type != DT_TELESCOPE ) continue;
		// must have a dow/tod type thing
		//if ( ! ( di->m_hasType & DT_DOW ) &&
		//     ! ( di->m_hasType & DT_HOLIDAY ) )
		//	continue;
		if ( ! ( di->m_hasType & (specialTypes|DT_DOW) ) ) 
			continue;
		// a tod or tod range, like "until 10pm"
		if ( ! ( di->m_hasType & DT_TOD ) ) continue;
		// no month day
		if ( di->m_hasType & DT_DAYNUM ) 
			continue;
		// get section of the date
		Section *sd = di->m_section;
		// scan up until sentence section
		for ( ; sd ; sd = sd->m_parent ) 
			if ( sd->m_flags & SEC_SENTENCE ) break;
		// if no sentence we must be in javascript
		if ( m_contentType == CT_JS && ! sd ) continue;
		// sanity check otherwise
		if ( ! sd ) { char *xx=NULL;*xx=0; }
		// in a table? get the cell section then
		Section *cell = di->m_tableCell;
		// . detects "kitchen hours" etc. basically any "sub hours"
		// . we just exclude these dates for setting store hours
		bool hasKitchen = false;
		if  ( hasKitchenHours ( sd ) ) 
			hasKitchen = true;
		else if ( cell && hasKitchenHours ( cell->m_headColSection ) )
			hasKitchen = true;
		else if ( cell && hasKitchenHours ( cell->m_headRowSection ) )
			hasKitchen = true;
		// if no kitchen hours, do not set any flags
		if ( ! hasKitchen ) continue;
		// otherwise...
		di->m_flags |= DF_KITCHEN_HOURS;
		// and remove this
		di->m_flags &= ~DF_STORE_HOURS;
	}

	//
	// set DF_SUBSTORE_HOURS
	//
	// . for dates that telescope to the store hours we set this
	//   unless we have a specific daynum or list of daynums. daynum
	//   ranges are ok.
	//
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if not in body (i.e. from url)
		if ( ! (di->m_flags & DF_FROM_BODY) ) continue;
		// skip if not telescope. this algo only does telescopes!
		if ( di->m_type != DT_TELESCOPE ) continue;
		// skip if store hours already set
		if ( di->m_flags & DF_STORE_HOURS ) continue;
		// must have one store hours ptr in it
		int32_t j; for ( j = 0 ; j < di->m_numPtrs ; j++ ) 
			if ( di->m_ptrs[j]->m_flags & DF_STORE_HOURS ) break;
		if ( j >= di->m_numPtrs ) continue;
		// ok, we got store hours, do we have a single
		// daynum or list of daynums?
		if ( (di->m_hasType & DT_DAYNUM) &&
		     !(di->m_hasType & DT_RANGE_DAYNUM) )
			// do not inherit the store hours flag in that case
			continue;
		// otherwise we do!
		di->m_flags |= DF_SUBSTORE_HOURS;
	}
}

// set Date::m_maxYearGuess for each date that
// does not have a year. use the year of the nearest neighbor
// to determine it. allow for that year minus or plus one if 
// we also have a DOW. and also allow for that year minus one if
// we are from a month # greater than our neighbor that supplied
// the year, assuming he did have a month. so if they have a list
// like Dec 13th and the neighbor following is Jan 2nd 2011, we 
// allow the year 2010 for Dec 13th. and only consider non-fuzzy
// years. so neighbors must be non-fuzzy dates.
void Dates::setMaxYearGuesses ( ) {

	int32_t minYear = 9999;
	int32_t maxYear = 0000;
	// for drivechicago.com the "May-Sep '10" date throws us off. but if
	// we recognize that there are years from 2010-2011 on the page then
	// we can allow the dow/month/daynum-based event dates to imply one of
	// those years. we can set their Date::m_guessedYearFromDow and use 
	// that to set leftGuess/rightGuess for other dates.
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if we got no year
		if ( ! ( di->m_hasType & DT_YEAR) ) continue;
		// skip if we got no tod
		if ( ! ( di->m_hasType & DT_TOD ) ) continue;
		// skip if copyright
		if ( di->m_flags & DF_COPYRIGHT ) continue;
		if ( di->m_flags & DF_FUZZY     ) continue;
		if ( di->m_flags & DF_NONEVENT_DATE ) continue;
		if ( di->m_flags5 & DF5_IGNORE ) continue;
		// ok, record min/max of years for event dates
		if ( di->m_minYear < minYear ) minYear = di->m_minYear;
		if ( di->m_maxYear > maxYear ) maxYear = di->m_maxYear;
	}
	
	// now set m_dowBasedYear for each date that has a dow and a monthday
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// stop?
		if ( minYear == 9999 ) break;
		// to be sure, must be within 4 years
		if ( maxYear - minYear > 4 ) break;
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if we got a year
		if ( di->m_hasType & DT_YEAR) continue;
		// skip if we got no tod
		if ( ! ( di->m_hasType & DT_TOD ) ) continue;
		// skip if copyright
		if ( di->m_flags & DF_COPYRIGHT ) continue;
		if ( di->m_flags & DF_FUZZY     ) continue;
		if ( di->m_flags & DF_NONEVENT_DATE ) continue;
		if ( di->m_flags5 & DF5_IGNORE ) continue;
		// skip if no dow
		if ( ! ( di->m_hasType & DT_DOW ) ) continue;
		if ( ! ( di->m_hasType & DT_DAYNUM ) ) continue;
		// how does this happen?
		// it happened for "Sunday October 30, 5 - 8PM" for
		// http://96.30.56.90/~nytmpl/
		if ( di->m_dayNum <= 0 ) continue;
		if ( di->m_minDayNum <= 0 ) continue;
		if ( di->m_maxDayNum <= 0 ) continue;
		if ( di->m_minDayNum != di->m_maxDayNum ) continue;
		// just one dow
		if ( getNumBitsOn8(di->m_dowBits) != 1 ) continue;
		// . ok, calculate what year it must be on then!
		// . return 0 on issue, if none in range
		di->m_dowBasedYear = 
			calculateYearBasedOnDOW ( minYear,maxYear, di );
	}

	// scan all the dates
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if copyright
		if ( di->m_flags & DF_COPYRIGHT ) continue;
		if ( di->m_flags & DF_FUZZY     ) continue;
		// skip if we got a year
		if ( di->m_hasType & DT_YEAR ) continue;
		// skip if we have no daynum
		if ( ! ( di->m_hasType & DT_DAYNUM ) ) continue;
		// . ok, we need a year then...
		// . returns -1 if could not find any good guess
		di->m_maxYearGuess = guessMaxYear ( i );
	}
}

// . sometimes there are multiple years with the event dates
// . we don't always pick the biggest year, we pick the one closest to
//   our date topologically.
int32_t Dates::guessMaxYear ( int32_t i ) {

	// get it
	Date *di = m_datePtrs[i];

	int32_t leftGuess  = 0;
	int32_t rightGuess = 0;

	// if we are so fortunate
	if ( di->m_dowBasedYear ) return di->m_dowBasedYear;

	Date *dj = NULL;
	// scan for date to the left
	for ( int32_t j = i - 1 ; j >= 0 ; j-- ) {
		QUICKPOLL(m_niceness);
		dj = m_datePtrs[j];
		if ( ! dj ) continue;
		if ( dj->m_a < 0 ) continue;
		if ( dj->m_flags & DF_FUZZY ) continue;
		if ( dj->m_flags & DF_COPYRIGHT ) continue;
		if ( dj->m_flags & DF_NONEVENT_DATE ) continue;
		if ( dj->m_flags5 & DF5_IGNORE ) continue;
		// . must be an event with a tod. 
		// . fixes kgoradio.com from using "in Summer of 2009"
		if ( ! (dj->m_hasType & DT_TOD) ) continue;
		// need a year
		if ( dj->m_hasType & DT_YEAR ) {
			leftGuess = dj->m_maxYear;
			break;
		}
		if ( dj->m_dowBasedYear ) {
			leftGuess = dj->m_dowBasedYear;
			break;
		}
		// if he has a max year, that is good!
		//if ( dj->m_maxYearGuess > 0 ) {
		//	leftGuess = dj->m_maxYearGuess;
		//	break;
		//}
	}

	Date *dk = NULL;
	// scan for date to the left
	for ( int32_t j = i + 1 ; j < m_numDatePtrs ; j++ ) {
		QUICKPOLL(m_niceness);
		dk = m_datePtrs[j];
		if ( ! dk ) continue;
		if ( dk->m_a < 0 ) continue;
		if ( dk->m_flags & DF_FUZZY ) continue;
		if ( dk->m_flags & DF_COPYRIGHT ) continue;
		if ( dk->m_flags & DF_NONEVENT_DATE ) continue;
		if ( dk->m_flags5 & DF5_IGNORE ) continue;
		// . must be an event with a tod. 
		// . fixes kgoradio.com from using "in Summer of 2009"
		if ( ! (dk->m_hasType & DT_TOD) ) continue;
		// need a year
		if ( dk->m_hasType & DT_YEAR ) {
			rightGuess = dk->m_maxYear;
			break;
		}
		if ( dk->m_dowBasedYear ) {
			rightGuess = dk->m_dowBasedYear;
			break;
		}
		// if he has a max year, that is good!
		//if ( dk->m_maxYearGuess > 0 ) {
		//	rightGuess = dk->m_maxYearGuess;
		//	break;
		//}
	}

	// if only had one, use that
	if ( leftGuess  == 0 ) return rightGuess;
	if ( rightGuess == 0 ) return leftGuess;

	// if same, ok
	if ( leftGuess == rightGuess ) return leftGuess;

	Section **sp = m_sections->m_sectionPtrs;
	// which is closer?
	Section *s1 = sp[dj->m_a];
	Section *s2 = sp[dk->m_a];
	Section *sx = sp[di->m_a];
	// grow our date until contains s1 or s2
	for ( ; sx ; sx = sx->m_parent ) {
		QUICKPOLL(m_niceness);
		if ( sx->contains ( s1 ) ) break;
		if ( sx->contains ( s2 ) ) break;
	}
	if ( sx->contains(s1) && ! sx->contains(s2) )
		return leftGuess;
	if ( sx->contains(s2) && ! sx->contains(s1) )
		return rightGuess;
	// ok, its a tie... who cares then. return biggest then
	if ( leftGuess > rightGuess ) return leftGuess;
	return rightGuess;
}

// return the dow based year
int32_t Dates::calculateYearBasedOnDOW ( int32_t minYear, int32_t maxYear, Date *di ) {
	// if month is -1 must be a range or list, skip it
	if ( di->m_month < 0 ) return 0;
	// must have just one dow
	int32_t numDow = getNumBitsOn8(di->m_dowBits);
	if ( numDow != 1 ) return 0;
	int32_t month = di->m_month;
	// sanity check for month, 1 to 12 are legit
	if ( month <= 0 || month >= 13 ) { char *xx=NULL;*xx=0; }
	int32_t day   = di->m_minDayNum;
	// between 1 and 31 sanity check
	if ( day < 1 || day > 31 ) { char *xx=NULL;*xx=0; }
	// bit #0 to x
	int32_t dow = getHighestLitBit((unsigned char)(di->m_dowBits));
	// between 0 and 6
	if ( dow >= 7 ) { char *xx=NULL;*xx=0; }

	// . Jan 1, 2000 fell on a saturday (leap year)
	// . Jan 1, 2001 fell on a monday
	// . Jan 1, 2002 fell on a tuesday
	// . Jan 1, 2003 fell on a wednesday
	// . Jan 1, 2004 fell on a thursday (leap year)
	// . Jan 1, 2005 fell on a saturday
	// . Jan 1, 2006 fell on a sunday
	// . Jan 1, 2007 fell on a monday
	// . Jan 1, 2008 fell on a tuesday (leap year)
	// . Jan 1, 2009 fell on a thursday
	// . Jan 1, 2010 fell on a friday
	// . Jan 1, 2011 fell on a saturday
	// . Jan 1, 2012 fell on a sunday
	
	// how many days into the year are we (assume not leap year)?
	int32_t daysIn = 0;
	for ( int32_t i = 1 ; i < month ; i++ ) 
		daysIn += s_numDaysInMonth[i-1];
	// add in current daynum, subtract 1
	daysIn += day - 1;
	// what the dow of jan 1 then?
	dow -= (daysIn % 7);
	// wrap it up
	if ( dow < 0 ) dow += 7;
	// between 0 and 6
	if ( dow >= 7 ) { char *xx=NULL;*xx=0; }
	// jan 1 2008 was a tuesday  = 2
	// jan 1 2000 was a saturday = 6
	int32_t jan1dow = 6;
	// scan the years. include up to 1 year from now (spideredtime)
	for ( int32_t y = 2000 ; y <= 2030 ; y++ ) {
		// stop if b
		QUICKPOLL(m_niceness);
		// save it
		int32_t saved = jan1dow;
		// inc for compare now if in leap year and past feb
		if ( (y % 4)==0 && month >= 3 )
			saved = jan1dow + 1;
		// inc it for next year
		jan1dow++;
		// wrap back to sunday
		if ( jan1dow == 7 ) jan1dow = 0;
		// leap year?
		if ( (y % 4) == 0 ) jan1dow++;
		// wrap back to sunday
		if ( jan1dow == 7 ) jan1dow = 0;
		// skip if not in requested range
		if ( y <  minYear ) continue;
		if ( y >  maxYear ) break;
		// compare
		if ( saved != dow ) continue;
		// ok, got a match
		return y;
	}
	return 0;
}


/*
///////////////////////////////
//
//
// new date normalization code
//
//
///////////////////////////////

// what about Nov 15 - oct 14 2010 [[]] nov 2010 [[]] ...

// . try to print out date in this format:
//   Every 2nd&3rd Mon,Thu,Sat-Sun from Aug2,2010-Oct10,2011 1pm-3pm & 5pm-7pm

// . compare to outright listing:
//   Aug 2,4,8 2010 Oct 6,12,13 2011 1pm-3pm

//   TODO: Every 2nd&3rd Mon,Thu,Sat-Sun from Aug2,2010 2pm - Oct10,2011 4pm
//   * if 2nd day in range is the next day at like 2am or 4am we don't need it!

// . TODO: what about the 4th day of every month??? just list outright?

// . [dow/dowlist/dowrange] 
//   [monthdayyear,monthdayyearlist,monthdayyearrange,seasonyear,holidayyear] 
//   [tod/todrange/todlist/todlistofranges]

// . 1. for each dow and tod/todrange pair
// . 2. find the smallest monthdayyear interval that contains all dow points
// . 3. set the recurring dowbits. i.e. if the dow is monday and every 2nd 
//      monday is empty then zero out that dow bit. 1st/2nd/4rd/4th/5th/last
// . 4. find longest time_t interval that covers the necessary recurring dow
//      days without exception. store them all in an array. record a
//      min and max interval for each one. i.e. the min's endpoints are
//      the necessary dows. the max's endpoints are past those usually up
//      to the missing dows (or spider endpoints)
// . 5. do the same thing for the missing dows that we should have but
//      are probably holidays or seasons they are closed or whatever
// . 6. repeat for each dow
// . 7. combine each dow representation
// . 8. positive interval maxes should be intersected, mins should be unioned
// . 9. negative interval maxes should be intersected, mins should be unioned
// . A. if any positive min intersects a negative min of another dow
//      we can't really combine them because there is no mdy date we can
//      use to express the restrictive mdy range...
// . B. weight each of these representation with just listing the
//      dates outright in m-d-y format
// . C. represent intervals as months, seasons, holidays, etc. to get the
//      most compact representation


// . just fix the way it prints out now
// . do not print a tod or tod range if not m_minTod or m_maxTod
// . do not print a dow or dow range if we have a dom
// . do not print a dom range if we have an exact dom
bool Date::printTextNorm2 ( Interval **intervals ,
			    int32_t numIntervals ,
			    SafeBuf *sb ,
			    int32_t thisYear ) {

	char dbuf[256];
	dedup.set ( 8,0,16,dbuf,256,false,niceness,"dadbuf");
	// now combine CronDates in same month and same tod
	// and use the day bits to hold that info
	for ( int32_t i = 0 ; i < m_numIntervals ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// int16_tcut
		Interval *ii = intervals[i];
		// seconds relative to that day
		uint64_t tod1 = ii->m_a % 86400;
		uint64_t tod2 = tod1 + ii->m_b - ii->m-a;
		// . if more than 24 hours long... wtf?
		// . Aug 2, 2010 2pm - Oct 10,2011 4pm
		if ( tod2 - tod1 > 24*3600 ) {
			log("dates: interval > 24 hrs. special normalization "
			    "required.");
			printSpecial2 ();
			continue;
		}
		// grab a tod to process
		int64_t key64 = (tod1<<32) | tod2;
		// already did?
		if ( dedup.isInTable ( &key64 ) ) continue;
		// add it
		if ( ! dedup.addKey ( &key64 ) ) return false;
		// prcess that
		if ( ! processTODRange ( tod1, tod2 ) ) return false;
	}
	return true;
}

bool Date::processTODRange ( uint32_t TOD1 ,
			     uint32_t TOD2 ,
			     Interval **intervals ,
			     int32_t numIntervals ,
			     SafeBuf *sb ,
			     int32_t thisYear ) {


	// . map each day this year and next two years to a "rom"
	// . "rom" = recurrence of month
	// . list is it the 1st monday or 2nd monday of the month, etc.?
	// . basically this maps out 3 years worth of days
	char romMap[365*3];
	char dowMap[365*3];
	char hasTod[365*3];
	char month [365*3];
	char dayNum[365*3];
	int32_t nr = 0;

	// . make a day map for ALL of spidered year, plus following year.
	// . then just start combinating over all possible things like
	//   months, seasons, holidays, etc. to describe what is shown in
	//   in day map.

	// . list each day its on like:
	// . 2nd saturday in august 2011
	// . then we can merge two such beasts multiple ways:
	// . 2nd saturdays aug-oct 2011
	// . saturdays in august



	// . not in 2010
	// . not in spring,winter,fall 2011
	// . not in august, oct or dec 2011
	// . not saturdays,sundays in august,oct,dec 2012
	// . not 3rd saturdays in august,oct,dec 2012
	// . not christmas, thanksgiving 2011
	// . not aug 3, 2011 - feb 4 2012 (finally daymonth ranges in order)
	//summer"... start with years, then seasons, then months.
	// . then only tuesdays/wed/...
	// 



	// loop over all dows
	for ( int32_t dow = 0 ; dow < 7 ; dow++ ) {
		// ptrs to every thursday etc.
		Interval *dows[150];
		char      roms[150];
		int32_t ndows = 0;
		// get all the intervals that have a START time
		// that falls on this DOW
		for ( int32_t i = 0 ; i < m_numIntervals ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// int16_tcut
			Interval *ii = intervals[i];
			// seconds relative to that day
			uint32_t tod1 = ii->m_a % 86400;
			uint32_t tod2 = tod1 + ii->m_b - ii->m-a;
			// a match?
			if ( tod1 != TOD1 ) continue;
			if ( tod1 != TOD2 ) continue;
			// does it fall on the right dow? (day of week)
			if ( getDow ( ii->m_a ) != dow ) continue;
			// ok, store it
			hasTod[ddd] = 1;
		}
		
		// now loop through 1 through 6, where 6 stands
		// for the "last day {DOW} of the month" where
		// {DOW} is "dow".
		for ( int32_t r = 0 ; r <= 6 ; r++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// . 0 means the 1st "monday" etc of the month
			// . we use monday if dow is 0, 1 for tuesday, etc.
			for ( k = spiderDay ; ; k++ ) 
				if ( dowMap[k] == dow ) break;
			// init
			bool inPositive = false;
			Span spans[1000];
			int32_t numSpans = 0;
			Span *cursor = &spans[numSpans++];
			// . this is a time_t when page was spidered i guess
			//   but it is UTC and event is local time!!!
			//   TODO: fix!!!
			// . should be when we started the Intervals
			cursor->m_a = spiderStartTime; 
			cursor->m_b = 0;
			// . now advance from there
			// . check every monday, etc.
			for ( ; k < lastDay ; k += 7 ) {
				// skip if not right rom
				if ( romMap[k] != r ) continue;
				// extend current span
				cursor->m_b = dayStart[k] + 86400;
				// if we are not changing our cursor span...
				if (   hasTod[k] &&   inPositive ) continue;
				if ( ! hasTod[k] && ! inPositive ) continue;
				// fix it so day #k is excluded
				cursor->m_b = dayStart[k];
				// then toggle inPositive
				inPositive = ! inPositive;
				// make a new span
				cursor = &spans[++numSpans];
				// init it to start of day here
				cursor->m_a = dayStart[k];
				cursor->m_b = 0;
				cursor->m_inPositive = inPositive;
			}			
			// . set last one
			// . should be when we truncated the Intervals
			cursor->m_b = lastDayTime;
			// now we have a set of spans (intervals really)
			// and we can represent them in standard form:
			//  "Every 2nd&3rd Mon,Thu,Sat-Sun from Aug2,2010-
			//   Oct10,2011 1pm-3pm ..."
			// and the negative spans are times that are 
			// exceptions

			// next get the min and max of each endpoint, a and
			// b, for each negative span.

			// do this for each "r" and each "dow"
		}
		// . combine all curosors for each "r" value
		// . reduce the negative spans so they do not exclude 
		//   positives from the other r values
		// . maybe just
	}

	// hmm... maybe just mark up the daymap without looping over the
	// r values then try to describe the days being excluded. like
	// excludes weekends, and also excluded labor day...

	-- mark all the days and carve out the biggest exceptions first.
		like years, then seasons, month ranges, months, holidays...



	// now 

			// then try to alter the endpoints within in the
			// min and max values so that they align on something
			// nice like a holiday, a month, a season or a year
			// or the complement of a month or range of months...
			// so we can say only in "Aug-Oct" or something...

...


			// try to shift the boundaries of the negative spans
			// so that they align with something simple.
			

			
		}

	}
	

	return true;
}

// . create a CronDate class for every date Interval
// . has more attrbutes than a "struct tm" used by mktime()
// . has bit arrays so we can combine two together
class CronDate {
public:
	// year range, like 2011-2012 or 2011-2011
	int32_t m_year1;
	int32_t m_year2;
	// for month range (i.e. oct-dec)
	int32_t m_month1;
	int32_t m_month2;
	// days of month relative to m_month1 and m_month2 to make
	// a month range like "oct 11 - dec22"
	int32_t m_mday1;
	int32_t m_mday2;
	// what months we represent, jan-dec
	int32_t m_monthMask;
	// if m_month1==m_month2, these bits represent up to 31 days
	int32_t m_mdayMask;
	// sunday thru saturday, 1 bit each
	char m_dowEveryMask;
	// the time-of-day time range (i.e. 1pm-3pm)
	int32_t m_tod1;
	int32_t m_tod2;
	// everyweekend,everyweekday,every 1/2/3/4/5th mask
	int32_t m_bits;
	// how many syllables does it take to print out the current date
	// as represented by this class in english?
	int32_t m_numSylables;
	class EnglishDate *m_exceptions1;
	class EnglishDate *m_exceptions2;
};


bool Dates::printNormalizedTime ( Date *dx , 
				  Interval **intervals ,
				  int32_t numIntervals ,
				  SafeBuf *sb ,
				  int32_t thisYear ) {

	// return now if no intervals
	if ( numIntervals == 0 ) return true;

	int32_t cdsize = sizeof(CronDate);
	// make space for the CronDates
	int32_t need = cdsize * numIntervals;
	CronDate *cd1 = (struct tm *)mmalloc ( need );
	if ( ! cd1 ) return false;
	CronDate *cd2 = (struct tm *)mmalloc ( need );
	if ( ! cd2 ) { mfree ( cd1 , need ); return false; }

	// set for calling functions easier, like printTODRanges(j)
	m_sb  = sb;
	m_cd  = cd;
	m_numCronDates = numIntervals;

	// assign a month and daynum for each interval and dow for that year
	for ( int32_t i = 0 ; i < numIntervals ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// int16_tcut
		Interval *ii = intervals[i];
		// decode it
		struct tm *ts;
		struct tm1;
		struct tm2;
		ts = gmtime ( &ii->m_a );
		gbmemcpy ( &tm1 , ts , sizeof(tm) );
		ts = gmtime ( &ii->m_b );
		gbmemcpy ( &tm2 , ts , sizeof(tm) );
		// just copy it
		cd[i]->m_year1 = tm1->tm_year + 1900;
		cd[i]->m_year2 = tm2->tm_year + 1900;
		cd[i]->m_month1 = tm1->tm_mon;
		cd[i]->m_month2 = tm2->tm_mon;
		cd[i]->m_mday1 = tm1->tm_mday;
		cd[i]->m_mday2 = tm2->tm_mday;
		cd[i]->m_tod1 = ii->m_a % 24*3600;
		cd[i]->m_tod2 = ii->m_b % 24*3600;
		cd[i]->m_dowEveryMask       = 0;
		cd[i]->m_dowEveryFirstMask  = 0;
		cd[i]->m_dowEverySecondMask = 0;
		cd[i]->m_dowEveryThirdMask  = 0;
		cd[i]->m_dowEveryFourthMask = 0;
		cd[i]->m_dowEveryFifthMask  = 0;
		cd[i]->m_bits = 0;
		cd[i]->m_numSylables = -1;
		cd[i]->m_exceptions1 = NULL;
		cd[i]->m_exceptions2 = NULL;
		// scan months if multiple and or them in
		cd[i]->m_monthMask = 0;
		//cd[i]->m_monthMaskExceptions = 0;
		for ( int32_t j = tm1->m_mon ; j <= tm2->m_mon ; j++ )
			cd[i]->m_monthMask |= 1 << j;
		// same for days, BUT IFF SAME MONTH!
		cd[i]->m_mdayMask = 0;
		//cd[i]->m_mdayMaskExceptions = 0;
		if ( tm1->m_mon == tm2->m_mon )
			cd[i]->m_mdayMask = 1 << (tm->tm_mday);
	}

	// now combine CronDates in same month and same tod
	// and use the day bits to hold that info
	for ( int32_t i = 0 ; i < m_numCronDates ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// grab a tod to process
		int32_t tod = cd[i].m_tod1 % 24*3600;
		// already did?
		if ( dedup.isInTable ( &tod ) ) continue;
		// add it
		if ( ! dedup.addKey ( &tod ) ) return false;
		// . compress based on that tod
		// . combines all CronDates into a single CronDate 
		//   and represents them with monthday bits
		if ( ! compressCronDates ( tod ) ) return false;
	}

	// if like monday had multiple tods try to combine them...
	// like monday @ 3pm and monday @ 6pm should be 
	// "monday: 3pm, 6pm"

	return true;
}

// combine similar CronDates and set m_mdayBits to keep track of what
// days of the month we are representing.
bool Dates::compressCronDates ( int32_t todArg ) {

	CronDate *prev = NULL;

	// combine crondates with same tod and month and year range together
	for ( int32_t i = 0 ; i < m_numCronDates ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		CronDate *cd = &m_cd[i];
		// skip if nuked
		if ( cd->m_bits & CD_IGNORED ) continue;
		// new last
		CronDate *last = prev;
		// update prev for next guy
		prev = cd;
		// grab a tod to process
		int32_t tod = cd[i].m_tod1 % 24*3600;
		// must match
		if ( tod != todArg ) continue;
		// get last?
		if ( ! last ) { last = cd; continue; }
		// if not same month/year, skip
		if ( last->m_month1 != cd->m_month1 ) continue;
		if ( last->m_month2 != cd->m_month1 ) continue;
		if ( last->m_year1  != cd->m_year1  ) continue;
		if ( last->m_year2  != cd->m_year2  ) continue;
		// ok, set his month day bit and we can get rid of us
		last->m_mdayBits |= cd->m_mdayBits;
		// we are nuked!
		cd->m_bits |= CD_IGNORED;
	}

	//
	// . now compress the months together
	// . like "daily aug-sep"
	//
	prev = NULL;
	// combine crondates with same tod and month and year range together
	for ( int32_t i = 0 ; i < m_numCronDates ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		CronDate *cd = &m_cd[i];
		// skip if nuked
		if ( cd->m_bits & CD_IGNORED ) continue;
		// new last
		CronDate *last = prev;
		// update prev for next guy
		prev = cd;
		// grab a tod to process
		int32_t tod = cd[i].m_tod1 % 24*3600;
		// must match
		if ( tod != todArg ) continue;
		// get last?
		if ( ! last ) { last = cd; continue; }
		// if not same year, skip
		if ( last->m_year1  != cd->m_year1  ) continue;
		if ( last->m_year2  != cd->m_year2  ) continue;
		// if same every masks, combine
		if ( last->m_dowEveryMask       != cd->m_dowEveryMask       )
			continue;
		if ( last->m_dowEveryFirstMask  != cd->m_dowEveryFirstMask  )
			continue;
		if ( last->m_dowEverySecondMask != cd->m_dowEverySecondMask )
			continue;
		if ( last->m_dowEveryThirdMask  != cd->m_dowEveryThirdMask  )
			continue;
		if ( last->m_dowEveryFourthMask != cd->m_dowEveryFourthMask )
			continue;
		if ( last->m_dowEveryFifthMask  != cd->m_dowEveryFifthMask  )
			continue;
		// can't be a month hole between us!
		if ( last->m_month2 + 1 < cd->m_month1 ) continue;
		// ok, combine
		last->m_month2 = cd->m_month2;
		// eliminate us
		cd->m_bits |= CD_IGNORED;
		// . we also get his exceptions!
		// . a linked list of CronDates?
		if ( last->m_exceptions )
			last->m_exceptions->m_next = cd->m_exceptions;
		else
			last->m_exceptions = cd->m_exceptions;
	}

	

	// . set m_everyDowMask for every day of the week that has the event
	//   every time for that month.
	// . then combine two adjacent CronDates that are different months
	//   but have the same m_everyDowMask


	// . if more every dow days are used than excepted, then go for it
	// . include the m_dowEvery*Masks as well
	// . set those bits for a month if the # of matches outnumbers
	//   the number of exceptions
	// . but count over the period of a whole year or season...

	// . hmmm... maybe just consider using the ranges already given to us
	//   in the Dates existing format?

	// . maybe just use that and do not print certain aspects of it...

	












	// . keep a CronDate between different months to hold exceptions

	// . if all months can be combined for a particular year and the
	//   # of words used to represent their m_everyDowMask and their
	//   exceptions to that is the smallest score... then do it







	// . compare score to just 
	
	// . if two adjacent months have th




	// . set m_dowEveryBits if occurs every S|M|T|W|R|F|S of the month
	// . set m_dowEveryFriday bits etc.
	// . ignore days BEFORE the spider time!!!! TODO!!!
	// . setting these bits then allows us to combine two+ months


	// . after trying to set those bits, make an array of the exceptions
	//   and try to compress that array and see how many words/syllables
	//   it is....

	// . call these donut holes...
...








	// . HOW TO deal with exceptions???
	// . combine NOT event dates as well?
	// . then we need like 365*2 cron dates, set CD_NOT_EVENT date?
	// . or at least, we need that for every unique tod/todrange


	// . between every two CronDates that have the same tod/todrange
	//   but are 1+ day apart, we should have an "exception" CronDate, 
	//   that represents the down days between those two CronDates


	//   have a CronDate that is not!
	// . then that is used to represent the whole between the two

	// . 

	
	

	// 
		

}

	// scan our parent for us
	if ( 
	int32_t ptrNum = -1;
	for ( int32_t k = 0 ; k < parent->m_numPtrs ; k++ ) {
		QUICKPOLL(m_niceness);
		if ( parent->m_ptrs[k] != dp ) continue;
		ptrNum = k;
		break;
	}

	// get date to left of us
	Date 
	Date *left = NULL;
	for ( ; ! left && ppp ;  ppp = ppp->m_dateParent )
		if ( ptrNum > 0 ) left = parent->m_ptrs[ptrNum-1];
	

*/

/* old code

void Dates::setMinMaxYearsOnPage ( ) {
	int32_t minYear1 = 9999;
	int32_t maxYear1 = 0;
	// get current year... when the doc was spidered
	int32_t currentYear = getYear ( m_nd->m_spideredTime );

	// scan all the dates
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if copyright
		if ( di->m_flags & DF_COPYRIGHT ) continue;
		if ( di->m_flags & DF_FUZZY     ) continue;
		// skip if not in body (i.e. from url). no, allow url dates!
		//if ( di->m_a < 0 ) continue;
		// a year is easy?
		if ( ! ( di->m_hasType & DT_YEAR ) ) continue;
		// must have dow or month. otherwise we get crap like
		// Summer of 2011. 2010/2011. etc.
		if ( ! ( di->m_hasType & DT_DAYNUM ) &&
		     ! ( di->m_hasType & DT_MONTH  ) )
			continue;
		// skip if list or range of years. fixes kumeyay.com which
		// has a list of month/year pairs in links for the archives.
		if ( di->m_minYear != di->m_maxYear ) continue;
		// throw away ridiculous years. talking about historical dates
		// i would think is most likely. fixes "June 27, 1940" for
		// santafe.org/perl/.....
		if ( di->m_minYear < 2000 ) continue;
		// ok, use it
		if ( di->m_minYear < minYear1 ) minYear1 = di->m_minYear;
		if ( di->m_maxYear > maxYear1 ) maxYear1 = di->m_maxYear;
	}

	int32_t minYear2 = 9999;
	int32_t maxYear2 = 0;

	// now repeat the loop but for implied years, i.e. "Wed Nov 13"
	for ( int32_t i = 0 ; i < m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Date *di = m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if already has year
		if ( di->m_hasType & DT_YEAR ) continue;
		// one daynum and one dow is good
		if ( ! (di->m_hasType & DT_DAYNUM) ) continue;
		if ( ! (di->m_hasType & DT_MONTH ) ) continue;
		if ( di->m_minDayNum != di->m_maxDayNum ) continue;
		// if month is -1 must be a range or list, skip it
		if ( di->m_month < 0 ) continue;
		// must have just one dow
		int32_t numDow = getNumBitsOn8(di->m_dowBits);
		if ( numDow != 1 ) continue;
		int32_t month = di->m_month;
		// sanity check for month, 1 to 12 are legit
		if ( month <= 0 || month >= 13 ) { char *xx=NULL;*xx=0; }
		int32_t day   = di->m_minDayNum;
		// between 1 and 31 sanity check
		if ( day < 1 || day > 31 ) { char *xx=NULL;*xx=0; }
		// bit #0 to x
		int32_t dow = getHighestLitBit((unsigned char)(di->m_dowBits));
		// between 0 and 6
		if ( dow >= 7 ) { char *xx=NULL;*xx=0; }

		// . Jan 1, 2000 fell on a saturday (leap year)
		// . Jan 1, 2001 fell on a monday
		// . Jan 1, 2002 fell on a tuesday
		// . Jan 1, 2003 fell on a wednesday
		// . Jan 1, 2004 fell on a thursday (leap year)
		// . Jan 1, 2005 fell on a saturday
		// . Jan 1, 2006 fell on a sunday
		// . Jan 1, 2007 fell on a monday
		// . Jan 1, 2008 fell on a tuesday (leap year)
		// . Jan 1, 2009 fell on a thursday
		// . Jan 1, 2010 fell on a friday
		// . Jan 1, 2011 fell on a saturday
		// . Jan 1, 2012 fell on a sunday

		// how many days into the year are we (assume not leap year)?
		int32_t daysIn = 0;
		for ( int32_t i = 1 ; i < month ; i++ ) 
			daysIn += s_numDaysInMonth[i-1];
		// add in current daynum, subtract 1
		daysIn += day - 1;
		// what the dow of jan 1 then?
		dow -= (daysIn % 7);
		// wrap it up
		if ( dow < 0 ) dow += 7;
		// between 0 and 6
		if ( dow >= 7 ) { char *xx=NULL;*xx=0; }
		// jan 1 2008 was a tuesday  = 2
		// jan 1 2000 was a saturday = 6
		int32_t jan1dow = 2;
		// scan the years. include up to 1 year from now (spideredtime)
		for ( int32_t y = 2008 ; y <= currentYear+1 ; y++ ) {
			// stop if b
			QUICKPOLL(m_niceness);
			// save it
			int32_t saved = jan1dow;
			// inc for compare now if in leap year and past feb
			if ( (y % 4)==0 && month >= 3 )
				saved = jan1dow + 1;
			// inc it for next year
			jan1dow++;
			// leap year?
			if ( (y % 4) == 0 ) jan1dow++;
			// compare
			if ( saved != dow ) continue;
			// ok, got a match
			if ( y < minYear2 ) minYear2 = y;
			if ( y > maxYear2 ) maxYear2 = y;
		}
	}

	// assume none defined
	m_minYearOnPage = -1;
	m_maxYearOnPage = -1;

	// bail if none defined
	if ( minYear1 == 9999 && minYear2 == 9999 ) 
		return;


	// only use minYear2/maxYear2 if minYear1/maxYear1 not defined
	if ( minYear1 == 9999 && minYear2 != 9999 ) {
		m_minYearOnPage = minYear2;
		m_maxYearOnPage = maxYear2;
		return;
	}

	// only use minYear1/maxYear1 if minYear2/maxYear2 not defined
	if ( minYear1 != 9999 && minYear2 == 9999 ) {
		m_minYearOnPage = minYear1;
		m_maxYearOnPage = maxYear1;
		return;
	}

	// . ignore minYear2/maxYear2 if minYear1/maxYear1 defined
	// . yeah, like patpendergrass.com has Friday, December 17 but
	//   its talking about 2005 not 2010!

	//if ( maxYear2 > maxYear1 ) m_maxYearOnPage = maxYear2;
	//else                       m_maxYearOnPage = maxYear1;

	m_minYearOnPage = minYear1;
	m_maxYearOnPage = maxYear1;

	// sometimes they just explicitly list next year, like zvents, but
	// the other dates are actually the year before that. sea & the
	// invalid mariner listed 3/24/2010 but the other dates were 
	// like Friday, Oct 16 which is in 2009.
	if ( minYear2 + 1 == minYear1 )
		m_minYearOnPage = minYear2;
	if ( maxYear2 + 1 == minYear1 )
		m_minYearOnPage = maxYear2;

	return;
}
*/
