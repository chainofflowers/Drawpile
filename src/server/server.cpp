/*******************************************************************************

   Copyright (C) 2006 M.K.A. <wyrmchild@sourceforge.net>

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:
   
   Except as contained in this notice, the name(s) of the above copyright
   holders shall not be used in advertising or otherwise to promote the sale,
   use or other dealings in this Software without prior written authorization.
   
   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.

   ---

   For more info, see: http://drawpile.sourceforge.net/

*******************************************************************************/

#include "server.h"

#include "../shared/templates.h"
#include "../shared/protocol.defaults.h"

#include <getopt.h> // for command-line opts
#include <cstdlib>
#include <iostream>

Server::Server()
	: password(0),
	pw_len(0),
	user_limit(0),
	cur_users(0),
	hi_port(protocol::default_port),
	lo_port(protocol::default_port)
{
	#ifndef NDEBUG
	std::cout << "Server::Server()" << std::endl;
	#endif
}

Server::~Server()
{
	#ifndef NDEBUG
	std::cout << "Server::~Server()" << std::endl;
	#endif
}

uint8_t Server::getUserID()
{
	#ifndef NDEBUG
	std::cout << "Server::getUserID()" << std::endl;
	#endif
	
	for (int i=1; i != 256; i++)
	{
		if (!user_ids.test(i))
		{
			user_ids.set(i, true);
			return i;
		}
	}
	
	return 0;
}

uint8_t Server::getSessionID()
{
	#ifndef NDEBUG
	std::cout << "Server::getSessionID()" << std::endl;
	#endif
	
	for (int i=1; i != 256; i++)
	{
		if (!session_ids.test(i))
		{
			session_ids.set(i, true);
			return i;
		}
	}
	
	return 0;
}

void Server::freeUserID(uint8_t id)
{
	assert(user_ids.test(id) == true);
	
	#ifndef NDEBUG
	std::cout << "Server::freeUserID(" << id << ")" << std::endl;
	#endif
	
	user_ids.set(id, false);
}

void Server::freeSessionID(uint8_t id)
{
	assert(session_ids.test(id) == true);
	
	#ifndef NDEBUG
	std::cout << "Server::freeSessionID(" << id << ")" << std::endl;
	#endif
	
	session_ids.set(id, false);
}

void Server::cleanup(int rc)
{
	#ifndef NDEBUG
	std::cout << "Server::cleanup()" << std::endl;
	#endif
	
	// close listening socket
	lsock.close();
	
	// finish event system
	ev.finish();
	
	/*
	std::map<uint32_t, User>::iterator u(users.begin());
	for (; u != users.end(); u++)
	{
		delete (*u)->s;
	}
	*/
}

void Server::uWrite(uint32_t fd)
{
	#ifndef NDEBUG
	std::cout << "Server::uWrite(" << fd << ")" << std::endl;
	#endif
	
	//users[fd]
	User* u = users[fd];
	
	Buffer buf = u->buffers.front();
	
	int sb = u->s->send(
		buf.rpos,
		buf.canRead()
	);
	
	std::cout << "Sent " << sb << " bytes.." << std::endl;
	
	if (sb > 0)
	{
		buf.read(sb);
		
		// just to ensure we don't need to do anything for it.
		assert(buf.rpos == u->buffers.front().rpos);
		
		if (buf.left == 0)
		{
			// remove buffer
			delete [] buf.data;
			u->buffers.pop();
			
			// remove fd from write list if no buffers left.
			if (u->buffers.empty())
			{
				fClr(u->events, ev.write);
				if (u->events == 0)
					ev.remove(fd, u->events);
				else
					ev.modify(fd, u->events);
			}
		}
	}
	else if (sb == 0)
	{
		// nothing
	}
	else
	{
		std::cerr << "Error occured while sending to user: "
			<< static_cast<int>(u->id) << std::endl;
		
		fClr(u->events, ev.read);
		if (u->events == 0)
			ev.remove(fd, u->events);
		else
			ev.modify(fd, u->events);
	}
}

void Server::uRead(uint32_t fd)
{
	#ifndef NDEBUG
	std::cout << "Server::uRead(" << fd << ")" << std::endl;
	#endif
	
	User* u = users[fd];
	
	std::cout << "From user: " << static_cast<int>(u->id) << std::endl;
	
	if (u->input.data == 0)
	{
		std::cout << "Assigning buffer to user." << std::endl;
		size_t buf_size = 8196;
		u->input.setBuffer(new char[buf_size], buf_size);
		std::cout << "Can write: " << u->input.canWrite() << std::endl
			<< "Can read: " << u->input.canRead() << std::endl;
		
	}
	
	if (u->input.canWrite() == 0)
	{
		std::cerr << "User input buffer full!" << std::endl;
		return;
	}
	
	int rb = u->s->recv(
		u->input.wpos,
		u->input.canWrite()
	);
	
	if (rb > 0)
	{
		std::cout << "Received " << rb << " bytes.." << std::endl;
		
		u->input.write(rb);
	}
	else if (rb == 0)
	{
		std::cout << "User disconnected!" << std::endl;
		ev.remove(fd, ev.write|ev.read);
		freeUserID(u->id);
		users.erase(u->id);
	}
	else
	{
		std::cerr << "Error occured while reading from user: "
			<< static_cast<int>(u->id) << std::endl;
		
		// TODO (EAGAIN and such)
		
		ev.remove(fd, ev.read);
	}
}

void Server::getArgs(int argc, char** argv)
{
	#ifndef NDEBUG
	std::cout << "Server::getArgs(" << argc << ", **argv)" << std::endl;
	#endif
	
	int32_t opt = 0;
	
	while ((opt = getopt( argc, argv, "p:Vhc:")) != -1)
	{
		switch (opt)
		{
			/*
			case 'a': // address to listen on
				
				break;
			*/
			case 'p': // port to listen on
				lo_port = atoi(optarg);
				{
					char* off = strchr(optarg, '-');
					hi_port = (off != 0 ? atoi(off+1) : lo_port);
				}
				
				if (lo_port <= 1023 or hi_port <= 1023)
				{
					std::cerr << "Super-user ports not allowed!" << std::endl;
					exit(1);
				}
				
				break;
			/*
			case 'l': // localhost admin
				localhost_admin = true;
				break;
			*/
			case 'u': // user limit
				user_limit = atoi(optarg);
				break;
			case 'c': // password
				pw_len = strlen(optarg);
				password = new char[pw_len];
				memcpy(password, optarg, pw_len);
				break;
			case 'h': // help
				std::cout << "Syntax: dbsrv [options]\n\n"
					<< "Options...\n\n"
					<< "   -p [port]  listen on 'port' (1024 - 65535).\n"
					<< "   -h         This output (a.k.a. Help)" << std::endl;
				exit(1);
				break;
			case 'V': // version
				exit(0);
			default:
				std::cerr << "What?" << std::endl;
				exit(0);
		}
	}
}

int Server::init()
{
	#ifndef NDEBUG
	std::cout << "Server::init()" << std::endl;
	#endif
	
	#ifndef NDEBUG
	std::cout << "creating socket" << std::endl;
	#endif
	
	lsock.fd(socket(AF_INET, SOCK_STREAM, 0));
	#ifndef NDEBUG
	std::cout << "New socket: " << lsock.fd() << std::endl;
	#endif
	
	if (lsock.fd() == INVALID_SOCKET)
	{
		std::cerr << "failed to create a socket." << std::endl;
		return -1;
	}
	
	lsock.block(0); // nonblocking
	
	#ifndef NDEBUG
	std::cout << "binding socket address" << std::endl;
	#endif
	{ // limited scope
		bool bound = false;
		for (int bport=lo_port; bport < hi_port+1; bport++)
		{
			#ifndef NDEBUG
			std::cout << "trying: " << INADDR_ANY << ":" << bport << std::endl;
			#endif
			
			if (lsock.bindTo(INADDR_ANY, bport) == SOCKET_ERROR)
			{
				if (lsock.getError() == EBADF || lsock.getError() == EINVAL)
					return -1;
				
				// continue
			}
			else
			{
				bound = true;
				break;
			}
		}
		
		if (!bound)
		{
			std::cerr << "Failed to bind to any port." << std::endl;
			int e = lsock.getError();
			switch (e)
			{
				#ifndef WIN32
				case EADDRINUSE:
					std::cerr << "address already in use" << std::endl;
					break;
				case EADDRNOTAVAIL:
					std::cerr << "address not available" << std::endl;
					break;
				case EAFNOSUPPORT:
					std::cerr << "invalid address family" << std::endl;
					break;
				case ENOTSOCK:
					std::cerr << "not a socket" << std::endl;
					break;
				case EOPNOTSUPP:
					std::cerr << "does not support bind" << std::endl;
					break;
				case EISCONN:
					std::cerr << "already connected" << std::endl;
					break;
				case ENOBUFS:
					std::cerr << "insufficient resources" << std::endl;
					break;
				#endif // WIN32
				case EACCES:
					std::cerr << "can't bind to superuser sockets" << std::endl;
					break;
				default:
					std::cerr << "unknown error" << std::endl;
					break;
			}
			
			return -1;
		}
	}
	
	if (lsock.listen() == SOCKET_ERROR)
	{
		std::cerr << "Failed to open listening port." << std::endl;
		return -1;
	}
	
	std::cerr << "listening on: " << lsock.address() << ":" << lsock.port() << std::endl;
	
	sockets.reserve(10);
	
	ev.init();
	
	ev.add(lsock.fd(), ev.read);
	
	return 0;
}

int Server::run()
{
	#ifndef NDEBUG
	std::cout << "Server::run()" << std::endl;
	#endif
	
	Socket *nu;
	
	std::vector<uint32_t>::iterator si;
	
	#ifndef NDEBUG
	std::cout << "eternity" << std::endl;
	#endif
	
	EvList evl;
	
	int ec, evs;
	while (1) // yay for infinite loops
	{
		ec = ev.wait(5000);
		
		if (ec == 0)
		{
			// continue, no fds or time exceeded.
		}
		else if (ec == -1)
		{
			std::cout << "Error in event system." << std::endl;
			// TODO (error)
		}
		else
		{
			//evl = ev.getEvents( ec );
			#ifndef NDEBUG
			std::cout << "Events waiting: " << ec << std::endl;
			#endif
			
			/*
			evs = ev.triggered(lsock.fd());
			std::cout << "Server events: ";
			std::cout.setf ( std::ios_base::hex, std::ios_base::basefield );
			std::cout.setf ( std::ios_base::showbase );
			std::cout << evs;
			std::cout.setf ( std::ios_base::dec );
			std::cout.setf ( ~std::ios_base::showbase );
			std::cout << std::endl;
			*/
			
			if (ev.isset(lsock.fd(), ev.read))
			{
				#ifndef NDEBUG
				std::cout << "server socket triggered" << std::endl;
				#endif
				
				ec--;
				
				nu = lsock.accept();
				
				if (nu != 0)
				{
					uint8_t id = getUserID();
					#ifndef NDEBUG
					std::cout << "New connection, ";
					#endif
					if (id == 0)
					{
						#ifndef NDEBUG
						std::cout << "but server is full." << std::endl;
						#endif
						delete nu;
					}
					else
					{
						#ifndef NDEBUG
						std::cout << "assigned ID: " << static_cast<uint32_t>(id) << std::endl;
						#endif
						
						User *ud = new User;
						ud->s = nu;
						ud->id = id;
						
						sockets.push_back(nu->fd());
						
						fSet(ud->events, ev.read);
						ev.add(ud->s->fd(), ud->events);
						
						users[nu->fd()] = ud;
						user_id_map[id] = nu->fd();
						
						std::cout << "Known sockets: " << sockets.size() << std::endl;
					}
				}
				else
				{
					#ifndef NDEBUG
					std::cout << "New connection but no socket?" << std::endl;
					#endif
				}
				// new connection?
			}
			
			if (ec > 0)
			{
				#ifndef NDEBUG
				std::cout << "Triggered sockets left: " << ec << std::endl;
				#endif
				
				for (si = sockets.begin(); si != sockets.end(); si++)
				{
					std::cout << "Testing: " << *si << std::endl;
					if (ev.isset(*si, ev.read))
					{
						#ifndef NDEBUG
						std::cout << "Reading from client" << std::endl;
						#endif
						
						ec--;
						uRead(*si);
					}
					else if (ev.isset(*si, ev.write))
					{
						#ifndef NDEBUG
						std::cout << "Writing to client" << std::endl;
						#endif
						
						ec--;
						uWrite(*si);
					}
					
					if (ec == 0) break;
				}
				
				#ifndef NDEBUG
				if (ec > 0)
					std::cout << "Not in clients..." << std::endl;
				#endif
			}
			
			// do something
		}
		
		// do something generic?
	}
	
	#ifndef NDEBUG
	std::cout << "done?" << std::endl;
	#endif
	
	return 0;
}
