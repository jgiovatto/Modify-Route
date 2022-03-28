
// g++ ./mod_route.cc -o modify_route -W -Wall

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <memory.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/route.h>
#include <net/if.h>
#include <netinet/ether.h>

#define RT_MASK(x)   ((((struct sockaddr_in *) & ((x).rt_genmask)))->sin_addr.s_addr)
#define RT_GW(x)     ((((struct sockaddr_in *) & ((x).rt_gateway)))->sin_addr.s_addr)
#define RT_DST(x)    ((((struct sockaddr_in *) & ((x).rt_dst)))->sin_addr.s_addr)

#define SA_ADDR(x)   ((((struct sockaddr_in *) (x)))->sin_addr.s_addr)
#define SA_FAMILY(x) ((((struct sockaddr_in *) (x)))->sin_family)
#define SA_PORT(x)   ((((struct sockaddr_in *) (x)))->sin_port)


class KernelRouteModifier
{
  public:
   KernelRouteModifier();

   int get_if_index(const std::string sDeviceName);

   in_addr_t get_if_addr(const std::string sDeviceName);

   int get_hw_addr(struct ether_addr *eth, const std::string sDeviceName);

   int modify_route(const bool action, // true = add, false = del
                    const uint16_t metric, 
                    const in_addr_t dest, 
                    const in_addr_t netmask, 
                    const in_addr_t gateway,
                    const std::string sDeviceName);

  private:
    int ctrl_sock_;

};

KernelRouteModifier::KernelRouteModifier()
{
  if((ctrl_sock_ = socket (AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("failed to open ctrl socket");
    throw;
  }
}

int KernelRouteModifier::get_if_index(const std::string sDeviceName)
{
  struct ifreq ifr;
  bzero(&ifr, sizeof (ifr));
  strncpy(ifr.ifr_name, sDeviceName.c_str (), IFNAMSIZ);

  if (ioctl (ctrl_sock_, SIOCGIFINDEX, &ifr) < 0) {
    perror("failed to get if index");
    throw;
 }

 return (ifr.ifr_ifindex);
}


in_addr_t KernelRouteModifier::get_if_addr(const std::string sDeviceName)
{
  struct ifreq ifr;
  struct sockaddr_in * sin = (struct sockaddr_in *) &ifr.ifr_addr;

  bzero (&ifr, sizeof (ifr));
  strncpy (ifr.ifr_name, sDeviceName.c_str (), IFNAMSIZ);
  sin->sin_family = AF_INET;

  if (ioctl (ctrl_sock_, SIOCGIFADDR, &ifr) < 0) {
    perror("failed to get if addr");
    throw;
  }

  return SA_ADDR(sin);
}



int KernelRouteModifier::get_hw_addr(struct ether_addr *eth, const std::string sDeviceName)
{
  struct ifreq ifr;

  bzero (&ifr, sizeof (ifr));
  strncpy (ifr.ifr_name, sDeviceName.c_str(), IFNAMSIZ);

  if ((ioctl (ctrl_sock_, SIOCGIFHWADDR, &ifr)) < 0) {
    perror("failed to get hw addr");
    throw;
  }

  memcpy(eth, &ifr.ifr_hwaddr, sizeof (*eth));

  return (0);
}



int KernelRouteModifier::modify_route(const bool action, 
                                      const uint16_t metric, 
                                      const in_addr_t dest, 
                                      const in_addr_t netmask, 
                                      const in_addr_t gateway,
                                      const std::string sDeviceName)
{
  int result = -1;

  struct rtentry rt;
  bzero(&rt, sizeof (rt));

  rt.rt_flags = RTF_UP;
  rt.rt_metric = metric + 1;

  // dest ip addr/net
  SA_ADDR  (&rt.rt_dst) = dest;
  SA_FAMILY(&rt.rt_dst) = AF_INET;
  SA_PORT  (&rt.rt_dst) = 0;

  // netmask
  SA_ADDR  (&rt.rt_genmask) = netmask;
  SA_FAMILY(&rt.rt_genmask) = AF_INET;
  SA_PORT  (&rt.rt_genmask) = 0;

  // gateway
  SA_ADDR  (&rt.rt_gateway) = gateway;
  SA_FAMILY(&rt.rt_gateway) = AF_INET;
  SA_PORT  (&rt.rt_gateway) = 0;

  if (~netmask == 0) {
    rt.rt_flags |= RTF_HOST;
  }

  if (RT_GW (rt)) {
    rt.rt_flags |= RTF_GATEWAY;
  }

  if (RT_MASK (rt)) {
    const in_addr_t mask = ntohl(RT_MASK(rt));

    if ((rt.rt_flags & RTF_HOST) && mask != (in_addr_t) ~0) {
      fprintf (stderr, "not a host route\n");
      return result;
    }

    if (~(((mask & -mask) - 1) | mask) != (in_addr_t) 0) {
      fprintf (stderr, "bad mask\n");
      return result;
    }

    if (RT_DST (rt) & ~RT_MASK (rt)) {
      fprintf (stderr, "bad dest\n");
      return result;
    }
  }

  rt.rt_dev = strdup(sDeviceName.c_str());

  if(!rt.rt_dev) {
    fprintf (stderr, "bad dev\n");
    return result;
  }

  if (ioctl (ctrl_sock_, action ? SIOCADDRT : SIOCDELRT, &rt) < 0) {
    perror ("ioctl");
  }
  else {
    result = 0; 
  }

  if(rt.rt_dev) {
    free (rt.rt_dev);
  }

  return result;
}


int main (int argc, char *argv[])
{
   int opt;

   std::string sDeviceName;

   while((opt = getopt(argc, argv, "hd:")) != -1) 
    {
      switch (opt)
       {
         case 'd':
          sDeviceName = optarg;
          break;

         case 'h':
         default:
          fprintf(stderr, "Usage: %s  [-d] device name\n", argv[0]);
          return -1; 
       }
    }

   if(sDeviceName.empty())
     {
       printf("must specify device name. eg [-d lo]\n");

       return -1;
     }

   try 
    {
      KernelRouteModifier krm;

      printf("device %s index %d\n", sDeviceName.c_str(), krm.get_if_index(sDeviceName));

      in_addr in;
      in.s_addr = krm.get_if_addr(sDeviceName);
      printf("device %s addr  %s\n", sDeviceName.c_str(), inet_ntoa(in));

      ether_addr eth; 
      krm.get_hw_addr(&eth, sDeviceName);
      printf("device %s hwaddr  %s\n", sDeviceName.c_str(), ether_ntoa(&eth));

      const auto dest    = inet_addr("1.2.3.1");          // dest ip host/net
      const auto netmask = inet_addr("255.255.255.255");  // netmask
      const auto gateway = inet_addr("192.168.8.100");    // gateway if any otherwise 0.0.0.0
      const uint16_t metric = 10;

      if(krm.modify_route(true,         // add
                          metric,       // metric
                          dest,         // dest ip host/net
                          netmask,      // netmask
                          gateway,      // gateway
                          sDeviceName)) // device name
       {
         printf("error adding route\n");
       }
      else
       {
         printf("added route, delete in 10 sec\n");
       }

      sleep(10);

      if(krm.modify_route(false,        // delete
                          metric,       // metric
                          dest,         // dest ip host/net
                          netmask,      // netmask
                          gateway,      // gateway
                          sDeviceName)) // device name
       {
         printf("error delete route\n");
       }

      printf("bye\n");
    }
   catch (const std::exception & ex)
    {
      printf("caught: %s\n", ex.what());
    }

   return 0;
}
