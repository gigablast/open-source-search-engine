#include <errno.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>      // for opendir()
#include <dirent.h>         // for opendir()
#include <time.h>           // for time()
#include <ctype.h>
#include <sys/socket.h>  // inet_ntoa()
#include <netinet/in.h>  // inet_ntoa()
#include <arpa/inet.h>   // inet_ntoa()
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

char  g_name[64];

char *g_hosts[5000];
char *g_ips  [5000];
int32_t  g_numHosts = 0;

void setHosts () ;
void add      ( char *s );
void add      ( char *prefix , int32_t num );

void setS99Local             ();
void setEtcHosts             ();
void setEtcNetworkInterfaces ();
void setEtcSshSshdconfig     ();
void setEtcPasswd();
void setAlias ();
void setEtcResolv ();
void setKnownHosts ();


char *getIp                   ( char *hostname );

// usage . no args!
int main ( int argc , char *argv[] ) {

	// get hostname
	FILE *fd = fopen ("/etc/hostname","r");
	if ( ! fd ) {
		fprintf(stderr,"could not open /etc/hostname\n");
		exit(-1);
	}

	if ( argc != 1 ) {
		fprintf(stderr,"does not take any args. put hostname in "
			"/etc/hostname\n");
		exit(-1);
	}

	// set the hostname into g_name[]
	fgets ( g_name , 60 , fd );

	// remove \n
	if ( g_name[strlen(g_name)-1] == '\n' )
		g_name[strlen(g_name)-1] = '\0';


	// . avoid all this junk for our routers
	// . these guys have special ip tables crap, routes, etc.
	if ( strcmp(g_name,"router0") == 0 ) return 0;
	if ( strcmp(g_name,"router1") == 0 ) return 0;
	if ( strcmp(g_name,"gk268") == 0 ) return 0; // router2
	if ( strcmp(g_name,"voyager2") == 0 ) return 0;
	if ( strcmp(g_name,"titan") == 0 ) return 0;


	// set startup to run mkraid 
	setS99Local();

	// . set the g_hosts and g_ips arrays
	// . set from /etc/hosts actually
	// . this must be called first to set g_hosts[], etc.
	setHosts();

	// set /etc/hosts, uses g_hosts[] now
	setEtcHosts ();

	// set /etc/network/interfaces
	setEtcNetworkInterfaces();

	// set /etc/ssh/sshd_config
	setEtcSshSshdconfig ();

	// . restart that
	// . ss machines just need a restart on eth1
	if ( g_name[0]=='s' && g_name[1] == 's' )
		system ( "/sbin/ifdown eth1 ; /sbin/ifup eth1");
	else
		system ( "/etc/init.d/networking restart");

	// restart sshd
	system ("/etc/init.d/ssh restart");

	// update /etc/passwd
	setEtcPasswd();

	// update /home/mwells/.alias
	setAlias ();

	// update /etc/resolv.conf
	setEtcResolv ();

	setKnownHosts ();

}

void setS99Local () {

	FILE *fd = fopen ("/etc/rcS.d/S99local", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /etc/rcS.d/S99local: %s\n",
			strerror(errno));
		return;
	}

	fprintf(fd,
"# increase network read buffer\n"
"# make even bigger for gigabit network later (10MB)\n"
"# echo 10048576 > /proc/sys/net/core/rmem_max\n"
"# this causes \"IP: frag: no memory for new fragment!\" failures in dmesg\n"
"# increase gigabit network read/write buffers otherwise we lose udp packets\n"
"echo 10000000 > /proc/sys/net/core/rmem_max\n"
"echo 5000000  > /proc/sys/net/core/wmem_max\n"
"\n"
		);

	fprintf(fd,
"/usr/sbin/elvtune -w 32 /dev/sda\n"
"/usr/sbin/elvtune -w 32 /dev/sdb\n"
"/usr/sbin/elvtune -w 32 /dev/sdc\n"
"/usr/sbin/elvtune -w 32 /dev/sdd\n"
"\n"
		);

	// make raid and mount if not already mounted
	// a lot of times after reboot something fails!
	fprintf(fd,
		"raidstop /dev/md0\n"
		"mkraid -c /etc/raidtab --really-force /dev/md0\n"
		"mount /dev/md0\n"
		"\n"
		);

	fclose(fd);
}

void setKnownHosts () {

	FILE *fd = fopen ("/home/mwells/.ssh/authorized_keys2", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /home/mwells/authorized_keys2: %s\n",
			strerror(errno));
		return;
	}

	// set authorized_keys2 for mwells
	fprintf ( fd , 
"ssh-dss AAAAB3NzaC1kc3MAAACBAK1lpodgz+4jbONiIUbl44UHTI8bieKplTajU05hgqsGqR6e8WykDMeKbgqGBGsy1bra/LHPh7l+g3C53qk6eejj38Xy320H60a1U5zUCs12kjCMTMruyrkM7ytIawdmNl8ZbnzrJVsdgUti5wpnssHE1u+PrXhjTWcAyk2wT8K3AAAAFQDrp5t8RXmUxBQnRUftZ6BJdkdhPwAAAIEAnEOhCwlZBQ/sHM0OrGzXYgzTlypl+c8HDoKmmkHWz8lsC2+wAt5HBUPvsNvuDxd1vL2VIm171MpHTpoId6Xc0baP5HRX/UHzuyWJvc1uHIuiTVypVuBe9v0FEzkigFHR4GjNucbmOGwotfhbudXFFsIND61RTfezSdRsoHf/oFgAAACAL34kqvvw9w6vkko7MMwHaE37Q4kQLHTNHNw/1PZDj6ivxaFekgpB55JTUEDs+IZ07P6gLv6dySLpeep0NTzdeUg2Y8IiRoFxUnN1uuIqf5Ngac5l7yvIZf/TvIM2diIrFRmXI9uBF8tnncfX01VQdONbWxESUUfEcf1mD8YVG6M= root@ss0\n"
"ssh-dss AAAAB3NzaC1kc3MAAACBANWHAJuWoH7pLAjtEqy0mqVi5sItvmlYO59LQ+a1UFETRBqhEkem9UMNmR+Ya+aQm9Dzo4RtjRWox+Z8tOCmLBymNfw/3x/ifxZwZZC8f4q81gYxuupHybHjONXEuu4gRYk1K/XNYSYFz93JuX5e90rb/IFl9MYKHtbWhPpDKQg3AAAAFQDVzEJweeVo2ROCjvSDQLHOZUURLQAAAIA39/oEFHywhxseB76dz5JeuWsu/p+Wex+0Yro4koWs5qkiUV91aHn1NNTSLWV2KZcSTwanfsoQ4QP7QZPDJyMQtd7MZE9d/5ZNiEVUMcpF5NBVg5ZQ4Hb3L8k5m/NMjLsgC2AEnMOnB1UT3lJgU33hXvivqAWZkzIwxFJsNwdJ0wAAAIBfAHl7wzdF9kOXB5LD87OJu3Hx9y9WS4yCk9Fz6d8173rVBX5tmOvH7nHQQoS1/lV9vlIVaTQPVZFqnmXu0dqoIEUYRTDxNkZVsbbwYJcJCZG9YL10x7IPucOUmoqStXlMDyOjO4SspNOaMOD0+dXohfcNkaTnZdk/DfeelFBZvw== mwells@voyager\n"
		  );
	fclose(fd);

	// set our private/public key pairs too for mwells
	fd = fopen ("/home/mwells/.ssh/id_dsa.pub", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /home/mwells/id_dsa.pub: %s\n",
			strerror(errno));
		return;
	}
	fprintf ( fd , 
		  "ssh-dss AAAAB3NzaC1kc3MAAACBAK1lpodgz+4jbONiIUbl44UHTI8bieKplTajU05hgqsGqR6e8WykDMeKbgqGBGsy1bra/LHPh7l+g3C53qk6eejj38Xy320H60a1U5zUCs12kjCMTMruyrkM7ytIawdmNl8ZbnzrJVsdgUti5wpnssHE1u+PrXhjTWcAyk2wT8K3AAAAFQDrp5t8RXmUxBQnRUftZ6BJdkdhPwAAAIEAnEOhCwlZBQ/sHM0OrGzXYgzTlypl+c8HDoKmmkHWz8lsC2+wAt5HBUPvsNvuDxd1vL2VIm171MpHTpoId6Xc0baP5HRX/UHzuyWJvc1uHIuiTVypVuBe9v0FEzkigFHR4GjNucbmOGwotfhbudXFFsIND61RTfezSdRsoHf/oFgAAACAL34kqvvw9w6vkko7MMwHaE37Q4kQLHTNHNw/1PZDj6ivxaFekgpB55JTUEDs+IZ07P6gLv6dySLpeep0NTzdeUg2Y8IiRoFxUnN1uuIqf5Ngac5l7yvIZf/TvIM2diIrFRmXI9uBF8tnncfX01VQdONbWxESUUfEcf1mD8YVG6M= root@ss0\n" );
	fclose ( fd );

	// mwells private key
	fd = fopen ("/home/mwells/.ssh/id_dsa", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /home/mwells/id_dsa: %s\n",
			strerror(errno));
		return;
	}
	fprintf ( fd , 
"-----BEGIN DSA PRIVATE KEY-----\n"
"MIIBuwIBAAKBgQCtZaaHYM/uI2zjYiFG5eOFB0yPG4niqZU2o1NOYYKrBqkenvFs\n"
"pAzHim4KhgRrMtW62vyxz4e5foNwud6pOnno49/F8t9tB+tGtVOc1ArNdpIwjEzK\n"
"7sq5DO8rSGsHZjZfGW586yVbHYFLYucKZ7LBxNbvj614Y01nAMpNsE/CtwIVAOun\n"
"m3xFeZTEFCdFR+1noEl2R2E/AoGBAJxDoQsJWQUP7BzNDqxs12IM05cqZfnPBw6C\n"
"pppB1s/JbAtvsALeRwVD77Db7g8Xdby9lSJte9TKR06aCHel3NG2j+R0V/1B87sl\n"
"ib3NbhyLok1cqVbgXvb9BRM5IoBR0eBozbnG5jhsKLX4W7nVxRbCDQ+tUU33s0nU\n"
"bKB3/6BYAoGAL34kqvvw9w6vkko7MMwHaE37Q4kQLHTNHNw/1PZDj6ivxaFekgpB\n"
"55JTUEDs+IZ07P6gLv6dySLpeep0NTzdeUg2Y8IiRoFxUnN1uuIqf5Ngac5l7yvI\n"
"Zf/TvIM2diIrFRmXI9uBF8tnncfX01VQdONbWxESUUfEcf1mD8YVG6MCFDZr9x3S\n"
"gG97b2U+XB8PUr5BScuO\n"
		  "-----END DSA PRIVATE KEY-----\n" );
	fclose ( fd );
	fd = fopen ("/home/mwells/.ssh/known_hosts", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /home/mwells/known_hosts: %s\n",
			strerror(errno));
		return;
	}
	// generate known_hosts
	for ( int32_t i = 0 ; i < g_numHosts ; i++ ) 
		fprintf(fd,"%s,%s ssh-rsa AAAAB3NzaC1yc2EAAAABIwAAAIEA8BS6BJTV3Ial3aiX0aMp3fCA"
			"e+cW23fk7E4lmrPJqcR0bYZR9yyvM3B0MdI2UWxo+NQ102gXprStZfvgKff0yZpdl0+hNfnseJiOE4OA"
			"BvwMKI8PIHKC35Oru+9DE2ITyEgUrriTig51JT9KCfHk6LaqLM83+yr8+Mr63LDSEI0=\n" ,
			g_hosts[i],g_ips[i]);
	fclose(fd);



	//
	// same for root
	//

	fd = fopen ("/root/.ssh/authorized_keys2", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /root/.ssh/authorized_keys2: %s\n",
			strerror(errno));
		return;
	}

	// set authorized_keys2 for root
	fprintf ( fd , 
"ssh-dss AAAAB3NzaC1kc3MAAACBAK1lpodgz+4jbONiIUbl44UHTI8bieKplTajU05hgqsGqR6e8WykDMeKbgqGBGsy1bra/LHPh7l+g3C53qk6eejj38Xy320H60a1U5zUCs12kjCMTMruyrkM7ytIawdmNl8ZbnzrJVsdgUti5wpnssHE1u+PrXhjTWcAyk2wT8K3AAAAFQDrp5t8RXmUxBQnRUftZ6BJdkdhPwAAAIEAnEOhCwlZBQ/sHM0OrGzXYgzTlypl+c8HDoKmmkHWz8lsC2+wAt5HBUPvsNvuDxd1vL2VIm171MpHTpoId6Xc0baP5HRX/UHzuyWJvc1uHIuiTVypVuBe9v0FEzkigFHR4GjNucbmOGwotfhbudXFFsIND61RTfezSdRsoHf/oFgAAACAL34kqvvw9w6vkko7MMwHaE37Q4kQLHTNHNw/1PZDj6ivxaFekgpB55JTUEDs+IZ07P6gLv6dySLpeep0NTzdeUg2Y8IiRoFxUnN1uuIqf5Ngac5l7yvIZf/TvIM2diIrFRmXI9uBF8tnncfX01VQdONbWxESUUfEcf1mD8YVG6M= root@ss0\n"
"ssh-dss AAAAB3NzaC1kc3MAAACBANWHAJuWoH7pLAjtEqy0mqVi5sItvmlYO59LQ+a1UFETRBqhEkem9UMNmR+Ya+aQm9Dzo4RtjRWox+Z8tOCmLBymNfw/3x/ifxZwZZC8f4q81gYxuupHybHjONXEuu4gRYk1K/XNYSYFz93JuX5e90rb/IFl9MYKHtbWhPpDKQg3AAAAFQDVzEJweeVo2ROCjvSDQLHOZUURLQAAAIA39/oEFHywhxseB76dz5JeuWsu/p+Wex+0Yro4koWs5qkiUV91aHn1NNTSLWV2KZcSTwanfsoQ4QP7QZPDJyMQtd7MZE9d/5ZNiEVUMcpF5NBVg5ZQ4Hb3L8k5m/NMjLsgC2AEnMOnB1UT3lJgU33hXvivqAWZkzIwxFJsNwdJ0wAAAIBfAHl7wzdF9kOXB5LD87OJu3Hx9y9WS4yCk9Fz6d8173rVBX5tmOvH7nHQQoS1/lV9vlIVaTQPVZFqnmXu0dqoIEUYRTDxNkZVsbbwYJcJCZG9YL10x7IPucOUmoqStXlMDyOjO4SspNOaMOD0+dXohfcNkaTnZdk/DfeelFBZvw== mwells@voyager\n"
		  );
	fclose(fd);

	// set our private/public key pairs too for root
	fd = fopen ("/root/.ssh/id_dsa.pub", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /root/id_dsa.pub: %s\n",
			strerror(errno));
		return;
	}
	fprintf ( fd , 
		  "ssh-dss AAAAB3NzaC1kc3MAAACBAK1lpodgz+4jbONiIUbl44UHTI8bieKplTajU05hgqsGqR6e8WykDMeKbgqGBGsy1bra/LHPh7l+g3C53qk6eejj38Xy320H60a1U5zUCs12kjCMTMruyrkM7ytIawdmNl8ZbnzrJVsdgUti5wpnssHE1u+PrXhjTWcAyk2wT8K3AAAAFQDrp5t8RXmUxBQnRUftZ6BJdkdhPwAAAIEAnEOhCwlZBQ/sHM0OrGzXYgzTlypl+c8HDoKmmkHWz8lsC2+wAt5HBUPvsNvuDxd1vL2VIm171MpHTpoId6Xc0baP5HRX/UHzuyWJvc1uHIuiTVypVuBe9v0FEzkigFHR4GjNucbmOGwotfhbudXFFsIND61RTfezSdRsoHf/oFgAAACAL34kqvvw9w6vkko7MMwHaE37Q4kQLHTNHNw/1PZDj6ivxaFekgpB55JTUEDs+IZ07P6gLv6dySLpeep0NTzdeUg2Y8IiRoFxUnN1uuIqf5Ngac5l7yvIZf/TvIM2diIrFRmXI9uBF8tnncfX01VQdONbWxESUUfEcf1mD8YVG6M= root@ss0\n" );
	fclose ( fd );

	// root private key
	fd = fopen ("/root/.ssh/id_dsa", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /root/id_dsa: %s\n",
			strerror(errno));
		return;
	}
	fprintf ( fd , 
"-----BEGIN DSA PRIVATE KEY-----\n"
"MIIBuwIBAAKBgQCtZaaHYM/uI2zjYiFG5eOFB0yPG4niqZU2o1NOYYKrBqkenvFs\n"
"pAzHim4KhgRrMtW62vyxz4e5foNwud6pOnno49/F8t9tB+tGtVOc1ArNdpIwjEzK\n"
"7sq5DO8rSGsHZjZfGW586yVbHYFLYucKZ7LBxNbvj614Y01nAMpNsE/CtwIVAOun\n"
"m3xFeZTEFCdFR+1noEl2R2E/AoGBAJxDoQsJWQUP7BzNDqxs12IM05cqZfnPBw6C\n"
"pppB1s/JbAtvsALeRwVD77Db7g8Xdby9lSJte9TKR06aCHel3NG2j+R0V/1B87sl\n"
"ib3NbhyLok1cqVbgXvb9BRM5IoBR0eBozbnG5jhsKLX4W7nVxRbCDQ+tUU33s0nU\n"
"bKB3/6BYAoGAL34kqvvw9w6vkko7MMwHaE37Q4kQLHTNHNw/1PZDj6ivxaFekgpB\n"
"55JTUEDs+IZ07P6gLv6dySLpeep0NTzdeUg2Y8IiRoFxUnN1uuIqf5Ngac5l7yvI\n"
"Zf/TvIM2diIrFRmXI9uBF8tnncfX01VQdONbWxESUUfEcf1mD8YVG6MCFDZr9x3S\n"
"gG97b2U+XB8PUr5BScuO\n"
		  "-----END DSA PRIVATE KEY-----\n" );
	fclose ( fd );

	fd = fopen ("/root/.ssh/known_hosts", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /root/known_hosts: %s\n",
			strerror(errno));
		return;
	}
	// generate known_hosts
	for ( int32_t i = 0 ; i < g_numHosts ; i++ ) 
		fprintf(fd,"%s,%s ssh-rsa AAAAB3NzaC1yc2EAAAABIwAAAIEA8BS6BJTV3Ial3aiX0aMp3fCA"
			"e+cW23fk7E4lmrPJqcR0bYZR9yyvM3B0MdI2UWxo+NQ102gXprStZfvgKff0yZpdl0+hNfnseJiOE4OA"
			"BvwMKI8PIHKC35Oru+9DE2ITyEgUrriTig51JT9KCfHk6LaqLM83+yr8+Mr63LDSEI0=\n" ,
			g_hosts[i],g_ips[i]);
	fclose(fd);
	







	
	// write the ALL file into /etc/dsh/group/ALL only for ss0
	if ( strcmp(g_name,"ss0")!= 0 ) return;
	fd = fopen ("/a/ALL", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /a/ALL: %s\n",
			strerror(errno));
		return;
	}
	// . print each hostname into "ALL"
	// . they include titan, etc. too!
	for ( int32_t i = 0 ; i < g_numHosts ; i++ ) {
		// skip if internal
		if ( strstr(g_hosts[i],"i") ) continue;
		if ( ! getIp(g_hosts[i]) ) continue;
		fprintf(fd,"%s\n",g_hosts[i]);
	}
	fclose(fd);
	
}

void setEtcResolv () {

	FILE *fd = fopen ("/etc/resolv.conf", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /etc/resolv.conf: %s\n",
			strerror(errno));
		return;
	}

	// set sshd config
	fprintf ( fd , 
		  "#nameserver 10.5.0.2\n"
		  "nameserver 10.5.0.3\n"
		  );
	fclose(fd);
}

void setAlias () {

	FILE *fd = fopen ("/home/mwells/.alias", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /home/mwells/.alias: %s\n",
			strerror(errno));
		return;
	}

	// set sshd config
	fprintf ( fd ,
		  "alias shutdown=\"/sbin/shutdown -h now\"\n"
		  "alias ps=\"ps auxww\"\n"
		  "alias mv=\"mv -i\"\n"
		  "alias t=\"tail -f log???\"\n"
		  "alias tt=\"tail -f tmplog???\"\n"
		  "alias tp=\"tail -f proxylog\"\n"
		  "alias dps=\"dsh -c -f all 'ps auxww | grep gb | grep -v grep'\"\n" 
		  );
	fclose ( fd );
}

void setEtcPasswd () {
	
	FILE *fd = fopen ("/etc/passwd", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /etc/passwd: %s\n",
			strerror(errno));
		return;
	}

	// set sshd config
	fprintf ( fd ,
		  "root:$1$qCnW2Bjd$.tTgYBHgx1WutBN5XXOVX/:0:0:root:/root:/bin/bash\n"
		  "daemon:*:1:1:daemon:/usr/sbin:/bin/sh\n"
		  "bin:*:2:2:bin:/bin:/bin/sh\n"
		  "sys:*:3:3:sys:/dev:/bin/sh\n"
		  "sync:*:4:65534:sync:/bin:/bin/sync\n"
		  "games:*:5:60:games:/usr/games:/bin/sh\n"
		  "man:*:6:12:man:/var/cache/man:/bin/sh\n"
		  "lp:*:7:7:lp:/var/spool/lpd:/bin/sh\n"
		  "mail:*:8:8:mail:/var/mail:/bin/sh\n"
		  "news:*:9:9:news:/var/spool/news:/bin/sh\n"
		  "uucp:*:10:10:uucp:/var/spool/uucp:/bin/sh\n"
		  "proxy:*:13:13:proxy:/bin:/bin/sh\n"
		  "www-data:*:33:33:www-data:/var/www:/bin/sh\n"
		  "backup:*:34:34:backup:/var/backups:/bin/sh\n"
		  "list:*:38:38:Mailing\n"
		  "irc:*:39:39:ircd:/var/run/ircd:/bin/sh\n"
		  "gnats:*:41:41:Gnats\n"
		  "nobody:*:65534:65534:nobody:/nonexistent:/bin/sh\n"
		  "Debian-exim:!:102:102::/var/spool/exim4:/bin/false\n"
		  "mwells:$1$XihGnGcG$jgwnEEK5O72nra72SMDO00:1000:1000:Matt Wells,,,:/home/mwells:/bin/bash\n"
		  "sshd:!:100:65534::/var/run/sshd:/bin/false\n"
		  "ntp:!:104:104::/home/ntp:/bin/false\n" 
		  );
	fclose(fd);
}


void setEtcSshSshdconfig () {
	
	FILE *fd = fopen ("/etc/ssh/sshd_config", "w" );
	if ( ! fd ) {
		fprintf(stderr,"could not open /etc/ssh/sshd_config: %s\n",
			strerror(errno));
		return;
	}

	// set sshd config
	fprintf ( fd ,
		"# Package generated configuration file\n"
		"# See the sshd(8) manpage for defails\n"
		"\n"

		"# What ports, IPs and protocols we listen for\n"
		"Port 22\n"
		"# Use these options to restrict which interfaces/protocols sshd will bind to\n"
		"#ListenAddress ::\n"
		"#ListenAddress 0.0.0.0\n"
		"Protocol 2\n"
		"# HostKeys for protocol version 2\n"
		"HostKey /etc/ssh/ssh_host_rsa_key\n"
		"HostKey /etc/ssh/ssh_host_dsa_key\n"
		"#Privilege Separation is turned on for security\n"
		"UsePrivilegeSeparation yes\n"
		"\n"

		"# ...but breaks Pam auth via kbdint, so we have to turn it off\n"
		"# Use PAM authentication via keyboard-interactive so PAM modules can\n"
		"# properly interface with the user (off due to PrivSep)\n"
		"PAMAuthenticationViaKbdInt no\n"
		"# Lifetime and size of ephemeral version 1 server key\n"
		"KeyRegenerationInterval 3600\n"
		"ServerKeyBits 768\n"
		"\n"
		
		"# Logging\n"
		"SyslogFacility AUTH\n"
		"LogLevel INFO\n"
		"\n"

		"# Authentication:\n"
		"LoginGraceTime 600\n"
		"PermitRootLogin yes\n"
		"StrictModes yes\n"
		"\n"

		"RSAAuthentication yes\n"
		"PubkeyAuthentication yes\n"
		"#AuthorizedKeysFile	%%h/.ssh/authorized_keys\n"
		"\n"

		"# rhosts authentication should not be used\n"
		"RhostsAuthentication no\n"
		"# Don't read the user's ~/.rhosts and ~/.shosts files\n"
		"IgnoreRhosts yes\n"
		"# For this to work you will also need host keys in /etc/ssh_known_hosts\n"
		"RhostsRSAAuthentication no\n"
		"# similar for protocol version 2\n"
		"HostbasedAuthentication no\n"
		"# Uncomment if you don't trust ~/.ssh/known_hosts for RhostsRSAAuthentication\n"
		"#IgnoreUserKnownHosts yes\n"
		"\n"
		
		"# To enable empty passwords, change to yes (NOT RECOMMENDED)\n"
		"PermitEmptyPasswords no\n"
		"\n"
		
		"# Uncomment to disable s/key passwords \n"
		"#ChallengeResponseAuthentication no\n"
		"\n"
		
		"# To disable tunneled clear text passwords, change to no here!\n"
		"PasswordAuthentication yes\n"
		"\n"
		
		"# To change Kerberos options\n"
		"#KerberosAuthentication no\n"
		"#KerberosOrLocalPasswd yes\n"
		"#AFSTokenPassing no\n"
		"#KerberosTicketCleanup no\n"
		"\n"
		
		"# Kerberos TGT Passing does only work with the AFS kaserver\n"
		"#KerberosTgtPassing yes\n"
		"\n"
		
		"X11Forwarding no\n"
		"X11DisplayOffset 10\n"
		"PrintMotd no\n"
		"#PrintLastLog no\n"
		"KeepAlive yes\n"
		"#UseLogin no\n"
		"\n"
		
		"#MaxStartups 10:30:60\n"
		"#Banner /etc/issue.net\n"
		"#ReverseMappingCheck yes\n"
		"\n"
		
		"Subsystem	sftp	/usr/lib/sftp-server\n"
		"\n"
		
		"#lets keep our connections alive (matt wells)\n"
		"ClientAliveInterval 30\n"
		"#ClientAliveMaxCount 3\n"
		"\n"
		
		"#AllowUsers mwells@38.114.104.* mwells@70.90.210.249\n"
		"\n"
		  );

	fclose ( fd );
}
	

void setHosts () {

	//add("67.16.94.2 gigablast.com\n");
	add("207.251.60.162 gigablast.com\n");
	add("207.251.60.162 rooftop\n");
	
	//add("10.5.0.1 soekris");
	add("10.5.0.2 router0");
	add("10.5.0.3 router1");
	add("10.5.56.78 router2"); // gk268 to roadrunner

	add("10.5.56.77 router-cnsp");
	add("10.5.56.78 router-rr");

	// this dedicated server is at softlayer.com
	add("50.22.70.146 qcproxy1"); // query compression proxy
	add("50.22.121.60 qcproxy1b"); // alias
	add("50.22.121.61 qcproxy1c"); // alias
	add("50.22.70.146 gw1"); // query compression proxy
	add("69.64.70.68 gw2");

	// this dedicated server is at serverpronto.com
	add("65.111.171.90 scproxy1");
	add("64.22.106.82 scproxy2");
	add("64.22.106.83 scproxy2");

	add("75.160.49.8 mattshouse");


	add("10.5.0.2 proxy0");
	add("10.5.0.2 proxyi0");

	add("10.5.0.3 proxy1");
	add("10.6.0.3 proxyi1");

	// going away...
	//add("10.5.50.1 oldproxy0");
	//add("10.6.50.1 oldproxyi0");

	// try these:
	//add("10.6.0.2 proxyi0");

	// make gf49 the universal proxy
	//add("10.5.62.59 proxy");
	// this ip address receives email from the internet
	//add("10.5.0.5 epgin");
	// this ip address receives email to be sent out onto the
	// internet, from a local thunderbird of outlook client. i
	// guess that mail.gigablast.com should send its mail out
	// through the epg on this ip.
	//add("10.5.0.6 epgout");
	add("10.5.0.9 roomalert");

	add("10.5.54.47 mail");
	
	add("10.5.1.200 voyager\n");
	add("10.5.1.201 voyager2\n");
	add("10.5.1.202 voyager3\n");
	add("10.5.1.203 titan\n");
	add("10.5.1.24 galileo\n");

	// g
	for ( int32_t i = 0 ; i <= 18 ; i++ )
		add("g",i);

	// gb
	for ( int32_t i = 0 ; i <= 17 ; i++ ) 
		add("gb",i);
	
	// gf
	for ( int32_t i = 0 ; i <= 49 ; i++ ) 
		add("gf",i);
	
	// gk
	for ( int32_t i = 0 ; i <= 271 ; i++ )
		add("gk",i);
	
	// ss
	for ( int32_t i = 0 ; i <= 147 ; i++ )
		add("ss",i);
	
	// sp
	for ( int32_t i = 0 ; i <= 13 ; i++ ) 
		add("sp",i);

}

void add ( char *s ) {

	static char  s_buf[100000];
	static char *s_ptr = s_buf;

	char *src = s;
	char *dst = s_ptr;
	// save it
	g_ips [ g_numHosts ] = dst;
	// store it
	while ( isdigit(*src) || *src=='.')
		*dst++ = *src++;
	// term it
	*dst++ = '\0';

	// now the ip as a string
	while ( ! isalnum(*src ) ) src++;

	// save that too
	g_hosts [ g_numHosts ] = dst;
	// store it
	while ( *src && ! isspace(*src) )
		*dst++ = *src++;
	// term it
	*dst++ = '\0';

	// update buf ptr for next host
	s_ptr = dst;

	g_numHosts++;
}

// add as a partial
void add ( char *prefix , int32_t num ) {

	// make the hostname
	char buf[64];
	sprintf ( buf , "%s%"INT32"" , prefix , num );
	// get ip
	char *ips = getIp ( buf ) ;
	// make another buf
	char buf2[128];
	// store that
	sprintf ( buf2 , "10.5.%s %s%"INT32"" , ips , prefix , num );
	// add it
	add ( buf2 );
	// store the eth1 too
	sprintf ( buf2 , "10.6.%s %si%"INT32"" , ips , prefix , num );
	// add it
	add ( buf2 );
}	

void setEtcHosts () {

	FILE *fd = fopen ("/etc/hosts","w");
	if ( ! fd ) {
		fprintf(stderr,"could not open "
			"/etc/hosts: %s\n",
			strerror(errno));
		return;
	}

	//
	// special cases
	//

	// gk37 also needs this line
	// but i am not sure it won't mess other hosts up!
	// # sendmail needs this
	if ( strcmp(g_name,"gk37") == 0 ) {
		//fprintf(fd,"67.16.94.2 gigablast.com gk37\n");
		fprintf(fd,"67.16.94.2 gk37\n");
		fprintf(fd,"\n");
	}

	fprintf(fd,"10.5.1.100 cam0 # alarm monitor\n");
	fprintf(fd,"10.5.1.101 cam1 # west window\n");
	fprintf(fd,"10.5.1.102 cam2 # backyard\n");
	fprintf(fd,"10.5.1.103 cam3 # east window\n");
	fprintf(fd,"10.5.1.104 cam4 # inside safe\n");
	fprintf(fd,"10.5.1.105 cam5 # dark room\n" );
	fprintf(fd,"10.5.1.110 cam10 # west pole\n");
	fprintf(fd,"10.5.1.111 cam11 # east pole\n");
	fprintf(fd,"\n");


	fprintf(fd,"10.5.77.10 phone0 # mwells polycom\n");
	fprintf(fd,"\n");
	
	// . our mail server, gk37, needs this
	//   so it accepts emails from asterisk@voyager2.gigablast.com
	// . really this only need be in gk37:/etc/hosts
	// . actually i think voyager2:/etc/hosts needs it too!!
	fprintf(fd,"# when asterisk sends emails they are from\n"
		"# asterisk@voyager2.gigablast.com, so gk37, the mail\n"
		"# server, needs to make sure that that exists\n");
	fprintf(fd,"10.5.1.201 voyager2.gigablast.com\n");
	fprintf(fd,"\n");

	//
	// end special cases
	//

	// just loop over hosts now
	for ( int32_t i = 0; i < g_numHosts ; i++ )
		fprintf(fd,"%s %s\n",g_ips[i],g_hosts[i]);

	fclose(fd);
}

void setEtcNetworkInterfaces() {

	// these are gateways and have special routes
	// mostly on eth1
	if ( strcmp(g_name,"router0") == 0 ) return;
	if ( strcmp(g_name,"router1") == 0 ) return;
	// router2!
	if ( strcmp(g_name,"gk268") == 0 ) return;

	// this does asterisk and uses router1 as its gateway
	if ( strcmp(g_name,"voyager2") == 0 ) return;

	// getIp() does not like these
	if ( strcmp(g_name,"titan") == 0 ) return;



	// get OUR ip
	char *ips = getIp ( g_name );

	FILE *fd = fopen ("/etc/network/interfaces","w");
	if ( ! fd ) {
		fprintf(stderr,"could not open "
			"/etc/network/interfaces: %s\n",
			strerror(errno));
		return;
	}

	fprintf(stderr,"ip for %s is %s\n",g_name,ips);

	fprintf ( fd ,
		  "auto lo\n"
		  "iface lo inet loopback\n"
		  "\n"
		  "auto eth0\n"
		  "iface eth0 inet static\n"
		  " address 10.5.%s\n" 
		  " netmask 255.255.0.0\n"
		  //" gateway 10.5.0.2\n"
		  " gateway 10.5.56.78\n" // gk268 roadrunner
		  "\n"
		  "auto eth1\n"
		  "iface eth1 inet static\n"
		  " address 10.6.%s\n"
		  " netmask 255.255.0.0\n"
		  "\n"
		  ,
		  ips ,
		  ips );

	fprintf(stderr,"Saved /etc/network/interfaces");

	fclose(fd);
	return;
}

char *getIp ( char *name ) {
	// must have 1-2 letters
	char *p = name;
	if ( !isalpha ( *p ) ) {
		//fprintf(stderr,"bad hostname %s no alpha\n",name);
		return NULL;
	}
	p++;
	// skip another alpha
	if ( isalpha (*p ) ) p++;
	// now must be a num
	if ( ! isdigit (*p) ) {
		//fprintf(stderr,"bad hostname %s no digit\n",name);
		return NULL;
	}
	// convert to num
	int32_t num = atoi(p);
	// eth0 is always 10.5.*.*
	// ss         = 10.5.50.*
	// g/gb/gf/sp = 10.5.52.*
	// gk         = 10.5.54.*
	// gk         = 10.5.56.*

	// eth1 is the same, but 10.6.*.*

	int32_t big = -1;
	if ( name[0] =='s' && name[1] == 's' )
		big = 50;
	if ( name[0] =='g' && name[1] == 'k' && num <= 199 )
		big = 54;
	if ( name[0] =='g' && name[1] == 'k' && num  > 199 ) {
		num -= 200;
		big = 56;
	}

	if ( name[0] =='g' && name[1] == 'b' )
		big = 58;

	if ( name[0] =='g' && name[1] == 'f' )
		big = 62;

	if ( name[0] =='g' && isdigit(name[1]) )
		big = 64;

	if ( name[0] =='s' && name[1] == 'p' )
		big = 66;

	if ( big == -1 ) {
		fprintf(stderr,"hostname %s is unsupported\n",
			name);
		exit(-1);
	}
	static char s_buf[200];
	sprintf ( s_buf ,"%"INT32".%"INT32"",big,10+num);
	return s_buf;
}
