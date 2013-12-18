#!/usr/bin/perl

# reads the official IANA charset list on stdin
# http://www.iana.org/assignments/character-sets
# generates iana_charset.h and iana_charset.cpp

# sets a flag on "supported" charsets
# ...the ones we recognise and that iconv will convert for us
# need supported_charsets.txt for this

my $curname;  # current charset name
my $csCount = 0;
my %charsets;

open(SUPPORTED, "supported_charsets.txt") 
    or die "Couldn't open supported_charsets.txt";

my %supportedCharsets;
while (<SUPPORTED>) {
    my $line = $_;
    chomp $line;
    chomp $line;
    if ($line =~ /^\s*(\d+)\s+([\w-]+)/){
	my $csEnum = $1;
	my $name = $2;
	print "Supported charset: $2 ($1)\n";
	$supportedCharsets{$csEnum} = 1;
    }
}

while (<>){
    my $line = $_;
    chomp $line;
    chomp $line;
    
    if ($line =~ /^Name:\s+([^\s]+)[^\[]*(\[([^\]]*)\])?/){
	#new charset
	$csCount++;
	#print "Charset: $1\n";
	#print "Ref: $3\n";
	$curname=$1;
	$charsets{$curname} = {};
	$charsets{$curname}->{ref} = $3;
	$charsets{$curname}->{names} = [];
	push @{$charsets{$curname}->{names}}, $curname;
	$charsets{$curname}->{preferred} = 0;
	$charsets{$curname}->{enum_name} = 0;
	next;
    }

    next unless defined($curname);

    if ($line =~ /^\s*$/){
	# end of charset
	undef $curname;
	next;
    }
    if ($line =~ /MIBenum:\s*(\d+)/){
	$charsets{$curname}->{enum_val} = $1;
	next
    }
    if ($line =~ /Alias:\s+([^\s]+)(\s+\(preferred MIME name\))?/){
	next if ($1 eq 'None');
	my $name = $1;
	push @{$charsets{$curname}->{names}}, $name;
	if (length($2)){
	    $charsets{$curname}->{preferred} = $#{@{$charsets{$curname}->{names}}};
	}
	if ($name =~/^cs/){
	    $charsets{$curname}->{enum_name} = $#{@{$charsets{$curname}->{names}}};
	}
    }

	
	
}

#additional aliases
push @{$charsets{"TIS-620"}->{names}}, "windows-874";
push @{$charsets{"Shift_JIS"}->{names}}, "x-sjis";



open CFILE, ">iana_charset.h" or die "Can't open iana_charset.h for writing";
print CFILE "// iana_charset.h\n";
print CFILE "// Generated automatically by parse_iana_charsets.pl ".gmtime()."\n";
print CFILE "// DO NOT EDIT!!!\n\n";
print CFILE "#ifndef IANA_CHARSET_H__\n";
print CFILE "#define IANA_CHARSET_H__\n";

print CFILE "enum eIANACharset{\n";
print CFILE "\tcsOther = 1, // unregistered character set\n";
print CFILE "\tcsUnknown = 2, // used as a default value\n";
foreach my $cs (sort {$a->{enum_val} <=> $b->{enum_val}} values %charsets){
    next if !defined($cs->{enum_val});
    my $enum_name = $cs->{names}[$cs->{enum_name}];
    $enum_name =~ s/[\-\_\:]+//sg;
    if ($enum_name !~ /^cs/){
	$enum_name = "cs".$enum_name;
	#print ">>>$enum_name: $cs->{enum_val}\n";
    }
    print CFILE "\t$enum_name = $cs->{enum_val},\n";
}
print CFILE "\tcsReserved = 3000\n};\n\n";

print CFILE "short get_iana_charset(char *cs, int len); \n";
print CFILE "char *get_charset_str(short cs); \n";
print CFILE "bool supportedCharset(short cs); \n";
print CFILE "void setSupportedCharsets(short *cs, int numCharsets);\n";
print CFILE "#endif\n";
close CFILE;

open CFILE, ">iana_charset.cpp" or die "Can't open iana_charset.cpp for writing";
print CFILE "// iana_charset.h\n";
print CFILE "// Generated automatically by parse_iana_charsets.pl ".gmtime()."\n";
print CFILE "// DO NOT EDIT!!!\n\n";
print CFILE "#include \"gb-include.h\"\n";
print CFILE "#include \"iana_charset.h\"\n";
print CFILE "#include \"HashTableX.h\"\n";
print CFILE "#include \"Conf.h\"\n";
print CFILE "#include \"hash.h\"\n";

print CFILE<<EOL;

typedef struct {
    char *name;
    char *mime;
    short mib_enum;
    char supported;
} IANACharset;

EOL
    

my $str = "static IANACharset s_charsets[] = {\n";
foreach my $cs (sort {$a->{enum_val} <=> $b->{enum_val}} values %charsets){
    next if !defined($cs->{enum_val});
    my $enum_name = $cs->{names}[$cs->{enum_name}];
    my $mime_name = $cs->{names}[$cs->{preferred}];

    # Microsoft bastards
    if ($mime_name eq 'KS_C_5601-1987'){
	$mime_name = 'x-windows-949';
    }

    if ($enum_name =~ /^cs/){
	#print "$enum_name: $cs->{enum_val}\n";
    }
    else{
	$enum_name =~ s/[\-\_\:]+//g;
	$enum_name = "cs".$enum_name;
	#print ">>>$enum_name: $cs->{enum_val}\n";
    }
    foreach my $name (@{$cs->{names}}){
	my $supported = $supportedCharsets{$cs->{enum_val}}?"1":"0";
	#print "supportedCharsets: ",%supportedCharsets,"\n";
	#print "$name $cs->{enum_val}: $supportedCharsets{$cs->{enum_val}}\n";
	$str .= "\t{\"$name\", \"$mime_name\", $cs->{enum_val}, $supported},\n";
	#print CFILE ",\n" if $name ne $cs->{names}[$#{@{$cs->{names}}}];
    }
}
# special case...not listed in IANA charsets, but found "in the wild"
#$str .= "\t{\"windows-874\", \"TIS-620\", 2259, 0},\n"; 
#$str .= "\t{\"x-sjis\", \"Shift_JIS\", 17, 1},\n"; 

chop $str;chop $str;
print CFILE $str;
print CFILE "\n};\n\n";
print CFILE <<EOL;

static HashTableX s_table;
static bool       s_isInitialized = false;

void reset_iana_charset ( ) {
	s_table.reset();
}

// Slightly modified from getTextEntity
short get_iana_charset(char *cs, int len)
{
    if (!s_isInitialized){
	// set up the hash table
	if ( ! s_table.set ( 8,4,4096,NULL,0,false,0,"ianatbl") )
	    return log("build: Could not init table of "
		       "IANA Charsets.");
	// now add in all the charset entries
	long n = (long)sizeof(s_charsets) / (long)sizeof(IANACharset);
	// turn off quickpolling
	char saved = g_conf.m_useQuickpoll;
	g_conf.m_useQuickpoll = false;
	for ( long i = 0 ; i < n ; i++ ) {
	    long long h = hash64Lower_a ( s_charsets[i].name, strlen(s_charsets[i].name) );
	    // store the charset index in the hash table as score
		if ( ! s_table.addTerm(&h, i+1) ) 
		return log("build: add term failed");
	}
	g_conf.m_useQuickpoll = saved;
	s_isInitialized = true;
    }
    long long h = hash64Lower_a ( cs , len );
    // get the entity index from table (stored in the score field)
    long i = (long) s_table.getScore ( &h );
    // return 0 if no match
    if ( i == 0 ) return csUnknown;
    // return the iso character
    return (short)s_charsets[i-1].mib_enum;
}

char *get_charset_str(short cs)
{
    int s=0;
    int e=sizeof(s_charsets)/sizeof(IANACharset)-2;
    int i;
    if (cs < s_charsets[s].mib_enum) return NULL;
    if (cs > s_charsets[e].mib_enum) return NULL;
    
    // Binary search
    while (1){
	// Check endpoints
	if (cs == s_charsets[s].mib_enum) return s_charsets[s].mime;
	if (cs ==s_charsets[e].mib_enum) return s_charsets[e].mime;

	// check midpoint
	i = (s+e)/2;
	if (cs ==s_charsets[i].mib_enum) return s_charsets[i].mime;
	
	// end of search 
	if ((e-s)<3) return NULL;
	
	// reset either endpoint
	if (cs < s_charsets[i].mib_enum){e = i-1;continue;}
	if (cs > s_charsets[i].mib_enum){s = i+1;continue;}
    }
    
}

// is this charset supported?
bool supportedCharset(short cs) {
    int s=0;
    int e=sizeof(s_charsets)/sizeof(IANACharset)-2;
    int i;
    if (cs < s_charsets[s].mib_enum) return false;
    if (cs > s_charsets[e].mib_enum) return false;
    
    // Binary search
    while (1){
	// Check endpoints
	if (cs == s_charsets[s].mib_enum) return s_charsets[s].supported;
	if (cs ==s_charsets[e].mib_enum) return s_charsets[e].supported;

	// check midpoint
	i = (s+e)/2;
	if (cs ==s_charsets[i].mib_enum) return s_charsets[i].supported;
	
	// end of search 
	if ((e-s)<3) return false;
	
	// reset either endpoint
	if (cs < s_charsets[i].mib_enum){e = i-1;continue;}
	if (cs > s_charsets[i].mib_enum){s = i+1;continue;}
    }	
}


EOL

close CFILE;
