# flickrsync

Sync folder of photos/videos with [Filckr](www.flickr.com) set of the same name

## Requirements

* [Flickcurl](http://librdf.org/flickcurl/): C library for the Flickr API
* [Qt](https://www.qt.io/) framework
* C++-11 compatible compiler (GCC)

## Usage
**flickrsync [OPTIONS] folder**
where OPTIONS are:

-n, --dry-run  --  Do not change anything, just show what should have been synced

-d, --download -- Download photos/videos missing from Flickr to folder

-r, --remove -- Delete photos/videos missing in local folder from Flickr

-s, --sort-by-title  -- Sort photos/videos by title after syncing

-o, --set-titles-by-date-taken -- Set photo titles by title daken (in form YYYYMMDD-HHMMSS)

-h, --help -- Print this help, then exit

Note, that the folder can be long path but only last folder name is used as Flickr set name

## Examples
```flickrsync -s /data/photos/Wedding```

syncs photos/videos in the folder /data/photos/Wedding to Flickr photoset named Wedding (creates the photoset if not existing) and sorts the photoset after sync based on photo titles (file basenames).

**flickrsync** can also be used to download all the photos/videos in set to folder. For example

```flickrsync -d /data/photos/Wedding```

downloads all missing photos/videos from the photoset Wedding to the folder /data/photos/Wedding (the folder can be empty at start to download all photoset.

## Authentication
**flickrsync** uses exactly the same authentication system as [Flickcurl](http://librdf.org/flickcurl/) tool.

So, to use flickrsync and flickcurl, you first have to [create Flickr API key](https://www.flickr.com/services/apps/create/apply/) and create Flickr OAuth tokens into ~/.flickcurl.conf file.

Please read how to do that from flickcurl [manual](http://librdf.org/flickcurl/api/flickcurl-auth.html).

Note, that you have to add &perms=delete to the end of the Flickr oauth authentication URL

## Building
To build the tool on modern Linux:
 1. clone the source, with:
 
    ```git clone https://github.com/martlaak-gmail/flickrsync.git```
 
 2. install requirements, can be done like this on Linux with apt package manager:
 
    ```sudo apt install flickcurl-utils libflickcurl-dev qt5-default libxml2-dev```
  
 3. in the flickrsync source foulder, build the tool, with:
 
    ```qmake; make```
 
