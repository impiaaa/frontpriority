# frontpriority
Automatically prioritize the process of the active X window

Doesn't need to be run as root as long as you follow the directions below

In order for this to work, your user needs to be able to elevate process
priority, which can be done by editing /etc/security/limits.conf. Here is
how to allow just your user to use "nice" levels lower than the default of 0:

    username        -       nice            -10

or the same, but a user group:

    @groupname      -       nice            -10

You can also do more advanced stuff. For example, set all users (except root)
to a low priority by default:

    *               -       priority        1

except for yourself:

    username        -       priority        0

and then allow your processes to go higher:

    username        -       nice            -10

Source: https://unix.stackexchange.com/q/8983

Should be run in the same X session as the one you'd like the adjustment to
take place in

