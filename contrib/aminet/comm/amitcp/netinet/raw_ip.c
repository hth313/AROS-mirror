/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * HISTORY
 * $Log$
 * Revision 1.1  2001/12/25 22:28:13  henrik
 * amitcp
 *
 * Revision 1.8  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.7  1993/05/17  00:16:44  ppessi
 * Changed RCS version. Added rcsid.
 *
 * Revision 1.6  1993/04/13  22:11:31  jraja
 * Removed The extra argument of the rip_usrreq(), since it has one too many!
 * The extra argument was passed to the raw_usrreq() which was wrong!
 *
 * Revision 1.5  93/04/11  22:26:48  22:26:48  jraja (Jarno Tapio Rajahalme)
 * Added STKARGFUN to protocol input & output functions (if used in protosw).
 * 
 * Revision 1.4  93/04/05  19:06:18  19:06:18  jraja (Jarno Tapio Rajahalme)
 * Changed storage of the spl functions  return values to type spl_t.
 * Added include for conf.h to every .c file.
 * 
 * Revision 1.3  93/03/22  16:59:32  16:59:32  jraja (Jarno Tapio Rajahalme)
 * Changed bcopy()s and bzero()s with word aligned pointers to
 * aligned_b(copy|zero) ar aligned_b(copy|zero)_const. The latter is for calls
 * in which the size is constant.
 * These can be disabled by defining NOALIGN.
 *  Converted bcopys doing structure copies (on aligned pointers) to structure
 * assignments, since at least SASC produces better code with assignment.
 * 
 * Revision 1.2  93/02/26  09:26:49  09:26:49  jraja (Jarno Tapio Rajahalme)
 * Made this compile with ANSI C (added prototypes).
 * Removed extra argument from call to raw_usrreq()
 * (yes, prototypes do wonders :-)
 * 
 * Revision 1.1  92/11/17  16:30:00  16:30:00  jraja (Jarno Tapio Rajahalme)
 * Initial revision
 * 
 */

/*
 * Copyright (c) 1982, 1986, 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)raw_ip.c	7.8 (Berkeley) 7/25/90
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/socketvar.h>
#include <sys/errno.h>
#include <sys/synch.h>

#include <net/if.h>
#include <net/route.h>
#include <net/raw_cb.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/in_pcb.h>

#include <netinet/raw_ip_protos.h>
#include <netinet/ip_output_protos.h>
#include <net/raw_usrreq_protos.h>
#include <kern/uipc_socket2_protos.h>

/*
 * Raw interface to IP protocol.
 */

struct	sockaddr_in ripdst = { sizeof(ripdst), AF_INET };
struct	sockaddr_in ripsrc = { sizeof(ripsrc), AF_INET };
struct	sockproto ripproto = { PF_INET };
/*
 * Setup generic address and protocol structures
 * for raw_input routine, then pass them along with
 * mbuf chain.
 */
void __stdargs
rip_input(m)
	struct mbuf *m;
{
	register struct ip *ip = mtod(m, struct ip *);

	ripproto.sp_protocol = ip->ip_p;
	ripdst.sin_addr = ip->ip_dst;
	ripsrc.sin_addr = ip->ip_src;
	if (raw_input(m, &ripproto, (struct sockaddr *)&ripsrc,
	  (struct sockaddr *)&ripdst) == 0) {
		ipstat.ips_noproto++;
		ipstat.ips_delivered--;
	}
}

/*
 * Generate IP header and pass packet to ip_output.
 * Tack on options user may have setup with control call.
 */
#define	satosin(sa)	((struct sockaddr_in *)(sa))
int __stdargs
rip_output(m, so)
	register struct mbuf *m;
	struct socket *so;
{
	register struct ip *ip;
	register struct raw_inpcb *rp = sotorawinpcb(so);
	register struct sockaddr_in *sin;

	/*
	 * If the user handed us a complete IP packet, use it.
	 * Otherwise, allocate an mbuf for a header and fill it in.
	 */
	if (rp->rinp_flags & RINPF_HDRINCL)
		ip = mtod(m, struct ip *);
	else {
		M_PREPEND(m, sizeof(struct ip), M_WAIT);
		ip = mtod(m, struct ip *);
		ip->ip_tos = 0;
		ip->ip_off = 0;
		ip->ip_p = rp->rinp_rcb.rcb_proto.sp_protocol;
		ip->ip_len = m->m_pkthdr.len;
		if (sin = satosin(rp->rinp_rcb.rcb_laddr)) {
			ip->ip_src = sin->sin_addr;
		} else
			ip->ip_src.s_addr = 0;
		if (sin = satosin(rp->rinp_rcb.rcb_faddr))
		    ip->ip_dst = sin->sin_addr;
		ip->ip_ttl = MAXTTL;
	}
	return (ip_output(m,
	   (rp->rinp_flags & RINPF_HDRINCL)? (struct mbuf *)0: rp->rinp_options,
	    &rp->rinp_route, 
	   (so->so_options & SO_DONTROUTE) | IP_ALLOWBROADCAST));
}

/*
 * Raw IP socket option processing.
 */
int
rip_ctloutput(op, so, level, optname, m)
	int op;
	struct socket *so;
	int level, optname;
	struct mbuf **m;
{
	int error = 0;
	register struct raw_inpcb *rp = sotorawinpcb(so);

	if (level != IPPROTO_IP)
		error = EINVAL;
	else switch (op) {

	case PRCO_SETOPT:
		switch (optname) {

		case IP_OPTIONS:
			return (ip_pcbopts(&rp->rinp_options, *m));

		case IP_HDRINCL:
			if (m == 0 || *m == 0 || (*m)->m_len < sizeof (int)) {
				error = EINVAL;
				break;
			}
			if (*mtod(*m, int *))
				rp->rinp_flags |= RINPF_HDRINCL;
			else
				rp->rinp_flags &= ~RINPF_HDRINCL;
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	case PRCO_GETOPT:
		*m = m_get(M_WAIT, MT_SOOPTS);
		switch (optname) {

		case IP_OPTIONS:
			if (rp->rinp_options) {
				(*m)->m_len = rp->rinp_options->m_len;
				aligned_bcopy(mtod(rp->rinp_options, caddr_t),
				    mtod(*m, caddr_t), (unsigned)(*m)->m_len);
			} else
				(*m)->m_len = 0;
			break;

		case IP_HDRINCL:
			(*m)->m_len = sizeof (int);
			*mtod(*m, int *) = rp->rinp_flags & RINPF_HDRINCL;
			break;

		default:
			error = EINVAL;
			m_freem(*m);
			*m = 0;
			break;
		}
		break;
	}
	if (op == PRCO_SETOPT && *m)
		(void)m_free(*m);
	return (error);
}

int
rip_usrreq(so, req, m, nam, /*rights,*/ control)
	register struct socket *so;
	int req;
	struct mbuf *m, *nam, /**rights,*/ *control;
{
	register int error = 0;
	register struct raw_inpcb *rp = sotorawinpcb(so);

	switch (req) {

	case PRU_ATTACH:
		if (rp)
			panic("rip_attach");
		MALLOC(rp, struct raw_inpcb *, sizeof *rp, M_PCB, M_WAITOK);
		if (rp == 0)
			return (ENOBUFS);
		aligned_bzero_const((caddr_t)rp, sizeof(*rp));
		so->so_pcb = (caddr_t)rp;
		break;

	case PRU_DETACH:
		if (rp == 0)
			panic("rip_detach");
		if (rp->rinp_options)
			m_freem(rp->rinp_options);
		if (rp->rinp_route.ro_rt)
			RTFREE(rp->rinp_route.ro_rt);
		if (rp->rinp_rcb.rcb_laddr)
			rp->rinp_rcb.rcb_laddr = 0;
		break;

	case PRU_BIND:
	    {
		struct sockaddr_in *addr = mtod(nam, struct sockaddr_in *);

		if (nam->m_len != sizeof(*addr))
			return (EINVAL);
		if ((ifnet == 0) ||
		    ((addr->sin_family != AF_INET) &&
		     (addr->sin_family != AF_IMPLINK)) ||
		    (addr->sin_addr.s_addr &&
		     ifa_ifwithaddr((struct sockaddr *)addr) == 0))
			return (EADDRNOTAVAIL);
		rp->rinp_rcb.rcb_laddr = (struct sockaddr *)&rp->rinp_laddr;
		rp->rinp_laddr = *addr;
		return (0);
	    }
	case PRU_CONNECT:
	    {
		struct sockaddr_in *addr = mtod(nam, struct sockaddr_in *);

		if (nam->m_len != sizeof(*addr))
			return (EINVAL);
		if (ifnet == 0)
			return (EADDRNOTAVAIL);
		if ((addr->sin_family != AF_INET) &&
		     (addr->sin_family != AF_IMPLINK))
			return (EAFNOSUPPORT);
		rp->rinp_rcb.rcb_faddr = (struct sockaddr *)&rp->rinp_faddr;
		rp->rinp_faddr = *addr;
		soisconnected(so);
		return (0);
	    }
	}
	error = raw_usrreq(so, req, m, nam, /*rights,*/ control);

	if (error && (req == PRU_ATTACH) && so->so_pcb)
		bsd_free(so->so_pcb, M_PCB);
	return (error);
}
