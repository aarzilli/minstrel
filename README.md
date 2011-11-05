# MOTIVATION

Minstrel is a minimalist music player with a command line interface born because:
1. I was tired of the time it took other music players to load their pretty user interface I never looked at
2. I couldn't find a music player supporting the only feature I care about: occasionally adding a specific song to an otherwise randomly generated queue

# FEATURES

* Full text search of songs in your library
* Support for all formats supported by gstreamer
* Desktop notifications for currently playing song
* Control through multimedia keys (if gnome-settings-daemon is running)

# COMPILING

Use make. You need the development version of this libraries:

* gstreamer
* glib
* ffmpeg
* libnotify
* sqlite3

Libnotify is only recommended, you can remove it from the Makefile and everything should keep working (except notifications, of course).

# INDEXING

Before you can use minstrel you need to add elements to its library. Use the command:

    minstrel index <diractory1> <directory2> ...
    
to add music to minstrel's library. Minstrel will create its library in `~/.minstrel`. If a library already exists it will be cleared first.

# PLAY QUEUE

Use the command:

    minstrel start
    
To start playing a randomly generated queue. You can move back and forth in the playing queue with:

    minstrel prev
    minstrel next
    
Every time you move past the end of the queue a new item will be added to it through random selection. You can stop playing by giving the command:

    minstrel stop
    
And pause (and resume playing) with:

    minstrel play
    
If gnome-settings-daemon is running and you have multimedia keys configured those will work too.

# SEARCHING AND ADDING TO QUEUE

The command:

    minstrel search <a query>
    
will display all the songs in your library that match the given full text query. If you want to add the result of a search to your queue do:

    minstrel search <a query> | minstrel add
    
or:

    minstrel search <a query>
    minstrel addlast
    
it's equivalent.

You can also add a song directly with:

    minstrel add <song id>
    
If you don't like to search by full text matching a query you can specify a boolean query with:

    minstrel where <query>
    
the syntax for this last command is that of Sqlite, the fields that you can use are:

* album
* artist
* album_artist
* comment
* composer
* copyright
* date
* disc
* encoder
* genre
* performer
* publisher
* title
* track
* filename
* any (full text index)