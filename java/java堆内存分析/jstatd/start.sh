#! /bin/bash
jstatd -J-Djava.security.policy=all.policy -J-Djava.rmi.server.logCalls=true -p1099 
