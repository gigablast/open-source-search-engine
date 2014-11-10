/// \file LanguagePages.h \brief Interface to the LanguagePage object.
///
/// Contains the interface to the object responsible for language specific
/// pages.
///
/// 2007 Sep 17 15:09:35
/// $ID$
/// $Author: John Nanney$
/// $Workfile$
/// $Log$

#ifndef LANGUAGEPAGES_H
#define LANGUAGEPAGES_H
#include "Lang.h"


class LanguagePages {
	public:

		/// Constructor, initializes object.
		LanguagePages();

		/// Destructor, cleans up when object is destroyed.
		~LanguagePages();

		/// Sets a language page from a given buffer.
		///
		/// It is up to the caller to insure that the page text
		/// is formatted properly, no parsing is done here.
		///
		/// @param lang the page's language
		/// @param pageText the page's text
		/// @param handOver whether or not the method will assume control of the buffer
		///
		/// @return true on success, false on memory allocation failure
		bool setLanguagePage(uint8_t lang, uint8_t *pageText, bool handOver = false);

		/// Sets a header entry from a given buffer.
		///
		/// @param lang the header's language
		/// @param pageText the header's text
		/// @param handOver whether or not the method will assume control of the buffer
		///
		/// @return true on success, false on memory allocation failure
		bool setLanguageHeader(uint8_t lang, uint8_t *pageText, bool handOver);

		/// Sets a footer entry from a given buffer.
		///
		/// @param lang the footer's language
		/// @param pageText the footer's text
		/// @param handOver whether or not the method will assume control of the buffer
		///
		/// @return true on success, false on memory allocation failure
		bool setLanguageFooter(uint8_t lang, uint8_t *pageText, bool handOver);

		/// Returns page text for a given language.
		///
		/// @param lang the language
		/// @param *len if not NULL, filled with the length of the page text
		///
		/// @return the page text, or NULL if no entry
		uint8_t *getLanguagePage(uint8_t lang, int32_t *len = NULL);

		/// Returns header text for a given language.
		///
		/// @param lang the language
		/// @param *len if not NULL, filled with the length of the header text
		///
		/// @return the header text, or NULL if no entry
		uint8_t *getLanguageHeader(uint8_t lang, int32_t *len = NULL);

		/// Returns footer text for a given language.
		///
		/// @param lang the language
		/// @param *len if not NULL, filled with the length of the footer text
		///
		/// @return the footer text, or NULL if no entry
		uint8_t *getLanguageFooter(uint8_t lang, int32_t *len = NULL);

		/// Reload all page, header, and footer entries from disk.
		void reloadPages(void);

	private:

		/// The array of page text entries.
		uint8_t *m_languagePages[MAX_LANGUAGES];

		/// The array of header text entries.
		uint8_t *m_languageHeaders[MAX_LANGUAGES];

		/// The array of footer text entries.
		uint8_t *m_languageFooters[MAX_LANGUAGES];

		/// Array of sizes of page text entries.
		/// Includes NULL, a value indicates ownership.
		uint32_t m_languageAllocated[MAX_LANGUAGES];

		/// Array of sizes of header text entries
		/// Includes NULL, a value indicates ownership.
		uint32_t m_languageAllocatedHeaders[MAX_LANGUAGES];

		/// Array of sizes of footer text entries
		/// Includes NULL, a value indicates ownership.
		uint32_t m_languageAllocatedFooters[MAX_LANGUAGES];

		/// Array of sizes of page text entries.
		uint32_t m_PageSize[MAX_LANGUAGES];

		/// Array of sizes of header text entries.
		uint32_t m_HeaderSize[MAX_LANGUAGES];

		/// Array of sizes of footer text entries.
		uint32_t m_FooterSize[MAX_LANGUAGES];

		/// Flag to protect entries during load.
		bool m_loading;
};

extern LanguagePages g_languagePages;

int uint8strlen(uint8_t *str);

#endif // LANGUAGEPAGES_H

