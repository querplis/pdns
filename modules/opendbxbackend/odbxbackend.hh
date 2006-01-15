/*
 *  PowerDNS OpenDBX Backend
 *  Copyright (C) 2005 Norbert Sendetzky <norbert@linuxnetworks.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */



#include <string>
#include <cstdlib>
#include <sstream>
#include <sys/time.h>
#include <pdns/dns.hh>
#include <pdns/utility.hh>
#include <pdns/dnspacket.hh>
#include <pdns/dnsbackend.hh>
#include <pdns/ueberbackend.hh>
#include <pdns/ahuexception.hh>
#include <pdns/arguments.hh>
#include <pdns/logger.hh>
#include <odbx.h>
#include "modules/ldapbackend/utils.hh"


#ifndef ODBXBACKEND_HH
#define ODBXBACKEND_HH


#define BUFLEN 512


using std::string;
using std::vector;



bool checkSlave( u_int32_t last, u_int32_t notified, SOAData* sd, DomainInfo* di );
bool checkMaster( u_int32_t last, u_int32_t notified, SOAData* sd, DomainInfo* di );


class OdbxBackend : public DNSBackend
{
	string m_myname;
	string m_qname;
	int m_default_ttl;
	bool m_qlog;
	odbx_t* m_handle;
	odbx_result_t* m_result;
	char m_escbuf[BUFLEN];
	char m_buffer[2*BUFLEN];

	bool getRecord();
	void execStmt( const char* stmt, unsigned long length, bool select );
	void getDomainList( const string& query, vector<DomainInfo>* list, bool (*check_fcn)(u_int32_t,u_int32_t,SOAData*,DomainInfo*) );
	string escape( const string& str );


public:

	OdbxBackend( const string& suffix="" );
	~OdbxBackend();

	void lookup( const QType& qtype, const string& qdomain, DNSPacket* p = 0, int zoneid = -1 );
	bool list( const string& target, int domain_id );
	bool get( DNSResourceRecord& rr );

	bool startTransaction( const string& domain, int domain_id );
	bool commitTransaction();
	bool abortTransaction();

	bool isMaster( const string& domain, const string& ip );
	bool getDomainInfo( const string& domain, DomainInfo& di );
	bool feedRecord( const DNSResourceRecord& rr );
	bool createSlaveDomain( const string& ip, const string& domain, const string& account );
	bool superMasterBackend( const string& ip, const string& domain, const vector<DNSResourceRecord>& nsset, string* account, DNSBackend** ddb );

	void getUpdatedMasters( vector<DomainInfo>* updated );
	void getUnfreshSlaveInfos( vector<DomainInfo>* unfresh );

	void setFresh( u_int32_t domain_id );
	void setNotified( u_int32_t domain_id, u_int32_t serial );
};



class OdbxFactory : public BackendFactory
{

public:

	OdbxFactory() : BackendFactory( "opendbx" ) {}


	void declareArguments( const string &suffix="" )
	{
		declare( suffix, "backend", "OpenDBX backend","mysql" );
		declare( suffix, "host", "Name or address of one or more DBMS server","127.0.0.1" );
		declare( suffix, "port", "Port the DBMS server is listening to","" );
		declare( suffix, "database", "Database name containing the DNS records","powerdns" );
		declare( suffix, "username","User for connecting to the DBMS","powerdns");
		declare( suffix, "password","Password for connecting to the DBMS","");

		declare( suffix, "sql-list", "AXFR query", "SELECT content, ttl, prio, type, domain_id, name FROM records WHERE domain_id=':id'" );

		declare( suffix, "sql-lookup", "Lookup query","SELECT content, ttl, prio, type, domain_id, name FROM records WHERE name=':name'" );
		declare( suffix, "sql-lookupid", "Lookup query with id","SELECT content, ttl, prio, type, domain_id, name FROM records WHERE name=':name' AND domain_id=':id'" );
		declare( suffix, "sql-lookuptype", "Lookup query with type","SELECT content, ttl, prio, type, domain_id, name FROM records WHERE type=':type' AND name=':name'" );
		declare( suffix, "sql-lookuptypeid", "Lookup query with type and id","SELECT content, ttl, prio, type, domain_id, name FROM records WHERE type=':type' AND name=':name' AND domain_id=':id'" );

		declare( suffix, "sql-zonedelete","Delete all records for this zone","DELETE FROM records WHERE domain_id=':id'" );
		declare( suffix, "sql-zoneinfo","Get domain info","SELECT d.id, d.name, d.master, d.last_check, d.notified_serial, d.type, r.content FROM domains AS d LEFT JOIN records AS r ON d.id=r.domain_id WHERE ( d.name=':name' AND r.type='SOA' ) OR ( d.name=':name' AND r.domain_id IS NULL )" );

		declare( suffix, "sql-transactbegin", "Start transaction", "BEGIN" );
		declare( suffix, "sql-transactend", "Finish transaction", "COMMIT" );
		declare( suffix, "sql-transactabort", "Abort transaction", "ROLLBACK" );

		declare( suffix, "sql-insert-slave","Add slave domain", "INSERT INTO domains ( type, name, master, account ) VALUES ( 'SLAVE', '%s', '%s', '%s' )" );
		declare( suffix, "sql-insert-record","Feed record into table", "INSERT INTO records ( domain_id, name, type, ttl, prio, content ) VALUES ( '%d', '%s', '%s', '%d', '%d', '%s' )" );

		declare( suffix, "sql-update-serial", "Set zone to notified", "UPDATE domains SET notified_serial='%d' WHERE id='%d'" );
		declare( suffix, "sql-update-lastcheck", "Set time of last check", "UPDATE domains SET last_check='%d' WHERE id='%d'" );

		declare( suffix, "sql-master", "Get master record for zone", "SELECT master FROM domains WHERE type='SLAVE' AND name=':name'" );
		declare( suffix, "sql-supermaster","Get supermaster info", "SELECT account FROM supermasters WHERE ip=':ip' AND nameserver=':ns'" );

		declare( suffix, "sql-infoslaves", "Get all unfresh slaves", "SELECT d.id, d.name, d.master, d.last_check, d.notified_serial, r.change_date, r.content FROM domains AS d LEFT JOIN records AS r ON d.id=r.domain_id WHERE ( d.type='SLAVE' AND r.type='SOA' ) OR ( d.type='SLAVE' AND r.domain_id IS NULL )" );
		declare( suffix, "sql-infomasters", "Get all updated masters", "SELECT d.id, d.name, d.master, d.last_check, d.notified_serial, r.change_date, r.content FROM domains AS d, records AS r WHERE d.type='MASTER' AND d.id=r.domain_id AND r.type='SOA'" );
	}


	DNSBackend* make( const string &suffix="" )
	{
		return new OdbxBackend( suffix );
	}
};


class OdbxLoader
{
	OdbxFactory factory;

public:

	OdbxLoader()
	{
		BackendMakers().report( &factory );
		L.log( " [OpendbxBackend] This is the opendbx module version "VERSION" ("__DATE__", "__TIME__") reporting", Logger::Info );
	}
};


static OdbxLoader odbxloader;



#endif /* ODBXBACKEND_HH */
