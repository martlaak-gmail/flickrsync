/*
 *
 * flickrsync utility - Sync folder to Filckr set of the same name
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>

#include <QDir>
#include <QRegularExpression>

#include <string>
#include <set>

#include <libxml/tree.h>
#include <flickcurl.h>
#include <curl/curl.h>

using namespace std;

const int CORRECT_DATE_BASED_NAME_LENGTH{15}; // Length of YYYYMMDD-HHMMSS
int verbose{1};
const char* program{"flickrsync"};

struct photoInfo {
  string title;
  string dateTaken;
  string description;
};

static void FlickrSyncMessageHandler(void *, const char *message)
{
  fprintf(stderr, "%s: ERROR: %s\n", program, message);
}

#define GETOPT_STRING "hnrdso"

static struct option long_options[] =
{
  /* name, has_arg, flag, val */
  {"help",    0, 0, 'h'},
  {"dry-run",  0, 0, 'n'},
  {"remove",  0, 0, 'r'},
  {"download-missing",  0, 0, 'd'},
  {"sort-by-title",  0, 0, 's'},
  {"set-titles-by-date-taken",  0, 0, 'o'},
  {NULL,      0, 0, 0}
};

static void printHelpString(void)
{
  printf("Sync folder of photos/videos to Flickr photoset\n");
  printf("Usage: %s [OPTIONS] folder\n"
         "where OPTIONS are:\n"
         "  -n, --dry-run                  Do not change anything, just show what should have been synced\n"
         "  -d, --download                 Download photos/videos missing from Flickr to folder\n"
         "  -r, --remove                   Delete photos/videos missing in local folder from Flickr\n"
         "                                 (note, that this needs delete permission given to the app by adding &perms=delete\n"
         "                                 to the end of Flickr oauth authentication URL during authentication setup)\n"
         "  -s, --sort-by-title            Sort photos/videos by title after syncing\n"
         "  -o, --set-titles-by-date-taken Set photo titles by title daken (in form YYYYMMDD-HHMMSS)\n"
         "  -h, --help                     Print this help, then exit\n\n"
         , program);
}

const string FLICKCURL_CONFIGFILE_NAME{".flickcurl.conf"};

string flickcurlConfigFile()
{
  auto home = getenv("HOME");
  if (home)
    return string(home) + '/' + FLICKCURL_CONFIGFILE_NAME;
  return FLICKCURL_CONFIGFILE_NAME;
}

string createPhotoSet(flickcurl* fc, const string& name, const string& primaryPhotoId)
{
  string setId;
  char* url = nullptr;
  if (auto id = flickcurl_photosets_create(fc, name.c_str(), nullptr, primaryPhotoId.c_str(), &url))
  {
    printf("New photoset '%s' created (id=%s, URL=%s)\n", name.c_str(), id, url);
    setId = id;
    free(url);
    free(id);
  }
  return setId;
}

bool addToSet(flickcurl* fc, const string& photoId, const string& setName, string* setId)
{
  if (setId->empty())
    *setId = createPhotoSet(fc, setName, photoId);
  else
    if (auto ret = flickcurl_photosets_addPhoto(fc, setId->c_str(), photoId.c_str()))
    {
      printf("ERROR: Unable to add uploaded photo/video 'id=%s' to set '%s': %d\n",
             photoId.c_str(), setName.c_str(), ret);
      return false;
    }
  return true;
}

static size_t curlWriteDataHandler(void *ptr, size_t size, size_t nmemb, void *stream)
{
  size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
  return written;
}

bool downloadFile(const string& url, const string& fileName)
{
  auto result{false};
  auto curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, curlWriteDataHandler);
  curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, true);
  if (auto fileHandle = fopen(fileName.c_str(), "wb"))
  {
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, fileHandle);
    auto res = curl_easy_perform(curl_handle);
    if (res == CURLE_OK)
      result = true;
    fclose(fileHandle);
  }
  curl_easy_cleanup(curl_handle);
  return result;
}

bool titleExistingInSet(const map<string,photoInfo>& photos, const string& title)
{
  for (const auto& photo : photos)
    if (photo.second.title == title)
      return true;
  return false;
}

bool isDateBasedName(const string& title)
{
  // Try to match YYYYMMDD-HHMMSS-n..
  return QRegularExpression("^\\d\\d\\d\\d\\d\\d\\d\\d-\\d\\d\\d\\d\\d\\d(-\\d*)?$").match(
        QString::fromStdString(title), 0, QRegularExpression::PartialPreferCompleteMatch).hasMatch();
}

string correctNameBasedOnDateTaken(const string& dateTaken)
{
  if (dateTaken.length() < 19) // date_taken is in format YYYY-MM-DD HH:MM:SS
    return "";
  return dateTaken.substr(0, 4) + dateTaken.substr(5, 2) + dateTaken.substr(8, 2) + "-" +
      dateTaken.substr(11, 2) + dateTaken.substr(14, 2) + dateTaken.substr(17, 2);
}

string addSuffixWhenDuplicateNamesExist(const string& title, const map<string,photoInfo>& photos)
{
  string correctedTitle = title;
  int prefix = 0;
  while (titleExistingInSet(photos, correctedTitle))
    correctedTitle = title + "-" + to_string(++prefix);
  return correctedTitle;
}

int main(int argc, char *argv[])
{
  flickcurl *fc = NULL;
  int rc = 0;
  int help = 0;
  int i;
  bool dryRun = false;
  bool removeNonExisting = false;
  bool downloadNonExisting = false;
  bool sortByTitle = false;
  bool renameByDateTaken = false;

  flickcurl_init();

  fc = flickcurl_new();
  if (!fc)
  {
    rc = 1;
    goto tidy;
  }

  flickcurl_set_error_handler(fc, FlickrSyncMessageHandler, NULL);

  if (!access(flickcurlConfigFile().c_str(), R_OK))
  {
    if (flickcurl_config_read_ini(fc, flickcurlConfigFile().c_str(), "flickr", fc, flickcurl_config_var_handler))
    {
      rc = 1;
      goto tidy;
    }
  }
  else
  {
    /* Check if the user has requested to see the help message */
    for (i = 0; i < argc; ++i)
    {
      if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        printHelpString();

      fprintf(stderr, "%s: Configuration file %s not found.\n\n"
                      "1. Visit http://www.flickr.com/services/api/keys/ to get an <API Key>\n"
                      "    and <Shared Secret>.\n"
                      "\n"
                      "2. Create %s in this format:\n"
                      "[flickr]\n"
                      "oauth_client_key=<Client key / API Key>\n"
                      "oauth_client_secret=<Client secret / Shared Secret>\n"
                      "\n"
                      "3. Call this program with:\n"
                      "  %s oauth.create\n"
                      "  (or %s oauth.create <Callback URL> if you understand and need that)\n"
                      "This gives a <Request Token> <Request Token Secret> and <Authentication URL>\n"
                      "\n"
                      "4. Visit the <Authentication URL> and approve the request to get a <Verifier>\n"
                      "\n"
                      "5. Call this program with the <Request Token>, <Request Token Secret>\n"
                      "    and <Verifier>:\n"
                      "  %s oauth.verify <Request Token> <Request Token Secret> <Verifier>\n"
                      "\n"
                      "This will write the configuration file with the OAuth access tokens.\n"
                      "See http://librdf.org/flickcurl/api/flickcurl-auth.html for full instructions.\n",
              program, flickcurlConfigFile().c_str(), flickcurlConfigFile().c_str(), program, program, program);
      rc = 1;
      goto tidy;
    }
  }

  while (!help)
  {
    int option_index = 0;
    auto c = getopt_long(argc, argv, GETOPT_STRING, long_options, &option_index);
    if (c == -1)
      break;

    switch (c)
    {
    case 'h':
      help = 1;
      break;

    case 'n':
      dryRun = true;
      break;

    case 'd':
      downloadNonExisting = true;
      break;

    case 'r':
      removeNonExisting = true;
      break;

    case 's':
      sortByTitle = true;
      break;

    case 'o':
      renameByDateTaken = true;
      break;
    }

  }

  argv += optind;
  argc -= optind;

  if (help || !argc)
  {
    printHelpString();
    exit(-1);
  }
  else
  {
    QDir folder(argv[0]);
    if (folder.exists())
    {
      string setName = folder.dirName().toStdString();
      printf("Starting to sync photos/videos from folder '%s' to Flickr...\n", argv[0]);
      map<string,string> photosInFolder;
      for (const auto& entry : folder.entryInfoList())
        if (entry.isFile())
        {
          string baseName = entry.baseName().toLower().toStdString();
          if (photosInFolder.count(baseName) != 0)
          {
            printf("ERROR: Photos/videos with duplicate basenames found (%s AND %s) - can not sync correctly\n",
                   entry.filePath().toStdString().c_str(), photosInFolder[baseName].c_str());
          }
          else
            photosInFolder[baseName] = entry.filePath().toStdString();
        }

      string setId;
      if (auto photoset_list = flickcurl_photosets_getList(fc, nullptr))
      {
        for(int i = 0; photoset_list[i]; i++)
          if (string(photoset_list[i]->title) == setName)
          {
            printf("Flickr photoset '%s' (id=%s) is already existing\n", setName.c_str(), photoset_list[i]->id);
            setId = photoset_list[i]->id;
          }
        flickcurl_free_photosets(photoset_list);
      }

      map<string,photoInfo> photosInSet;
      if (!setId.empty())
      {
        if (auto photos = flickcurl_photosets_getPhotos(fc, setId.c_str(), "date_upload,date_taken,description", -1, -1, -1))
        {
          for (int i = 0; photos[i]; i++)
          {
            auto description = photos[i]->fields[PHOTO_FIELD_description].string;
            photosInSet[photos[i]->id] = {photos[i]->fields[PHOTO_FIELD_title].string,
                                          photos[i]->fields[PHOTO_FIELD_dates_taken].string,
                                          description ? description : ""};
          }
          flickcurl_free_photos(photos);
        }
      }
      if (renameByDateTaken && photosInSet.size())
      {
        for (const auto& photoFile : photosInSet)
          if (!isDateBasedName(photoFile.second.title))
          {
            auto correctName = correctNameBasedOnDateTaken(photoFile.second.dateTaken);
            if (!correctName.empty())
            {
              if (photoFile.second.title.substr(0, CORRECT_DATE_BASED_NAME_LENGTH) != correctName)
              {
                correctName = addSuffixWhenDuplicateNamesExist(correctName, photosInSet);
                if (!dryRun)
                {
                  printf("Setting photo title based on date taken %s => %s\n", photoFile.second.title.c_str(),
                         correctName.c_str());
                  if (auto ret = flickcurl_photos_setMeta(fc, photoFile.first.c_str(),
                                                          correctName.c_str(),
                                                          photoFile.second.dateTaken.c_str()))
                    printf("ERROR: Unable to set photo %s title to %s: %d\n", photoFile.second.title.c_str(),
                           correctName.c_str(), ret);
                  else
                    photosInSet[photoFile.first].title = correctName;
                }
                else
                  printf("Need to set photo title based on date taken %s => %s\n", photoFile.second.title.c_str(),
                         correctName.c_str());
              }
            }
          }
      }
      map<string,string> uploadedPhotos;
      for (const auto& photoFile : photosInFolder)
        if (!titleExistingInSet(photosInSet, photoFile.first))
        {
          flickcurl_upload_params params;
          memset(&params, '\0', sizeof(flickcurl_upload_params));
          params.safety_level = 1;
          params.content_type = 1;
          params.hidden = 1;
          params.is_family = 1;
          params.title = photoFile.first.c_str();
          params.photo_file = photoFile.second.c_str();

          if (!dryRun)
          {
            printf("Uploading photo/video %s ...", params.photo_file);
            fflush(stdout);
            if (auto status = flickcurl_photos_upload_params(fc, &params))
            {
              printf("Done (id=%s)\n", status->photoid);
              uploadedPhotos[photoFile.first] = status->photoid;
              if (addToSet(fc, status->photoid, setName, &setId))
                photosInSet[status->photoid] = { photoFile.first, "", "" };
              flickcurl_free_upload_status(status);
            }
            else
              printf("Failed!\n");
          }
          else
          {
            printf("Need to upload photo %s\n", params.photo_file);
            uploadedPhotos[photoFile.first] = "-";
          }
        }
        else
          printf("Photo/video %s is already existing in set, skipping\n", photoFile.first.c_str());

      int downloaded = 0;
      int deleted = 0;
      auto photo = photosInSet.begin();
      while (photo != photosInSet.end())
      {
        if (photosInFolder.count(photo->second.title) == 0)
        {
          if (removeNonExisting)
          {
            if (!dryRun)
            {
              printf("Photo/video %s not existing in folder anymore - deleting\n", photo->second.title.c_str());
              if (auto ret = flickcurl_photos_delete(fc, photo->first.c_str()))
                printf("ERROR: Unable to delete photo/video %s (id=%s): %d\n", photo->second.title.c_str(),
                       photo->first.c_str(), ret);
              else
              {
                ++deleted;
                photo = photosInSet.erase(photo);
                continue;
              }
            }
            else
            {
              printf("Photo/video %s not existing in folder anymore - need to delete it\n", photo->second.title.c_str());
              ++deleted;
              photo = photosInSet.erase(photo);
              continue;
            }
          }
          else if (downloadNonExisting)
          {
            if (auto sizes = flickcurl_photos_getSizes(fc, photo->first.c_str()))
            {
              string filePath;
              string downloadUrl;
              for (int i = 0; sizes[i]; ++i)
              {
                if (strcmp(sizes[i]->media, "video") == 0 &&
                    strcmp(sizes[i]->label, "Video Original") == 0)
                {
                  filePath = folder.filePath(QString(photo->second.title.c_str()) + ".mp4").toStdString();
                  downloadUrl = sizes[i]->source;
                  break;
                }
                else if (strcmp(sizes[i]->media, "photo") == 0 &&
                         strcmp(sizes[i]->label, "Original") == 0)
                {
                  filePath = folder.filePath(QString(photo->second.title.c_str()) + ".jpg").toStdString();
                  downloadUrl = sizes[i]->source;
                }
              }
              if (!downloadUrl.empty())
              {
                if (!dryRun)
                {
                  printf("Starting to download photo/video file '%s' ...", filePath.c_str());
                  fflush(stdout);
                  if (downloadFile(downloadUrl, filePath))
                  {
                    printf("Done\n");
                    ++downloaded;
                  }
                  else
                    printf("Failed!\n");
                }
                else
                {
                  printf("Need to download photo/video file '%s'\n", filePath.c_str());
                  ++downloaded;
                }
              }
            }
          }
          else
            printf("WARNING: Photo/video %s not existing in folder anymore, specify -r to remove or -d to download these\n", photo->second.title.c_str());
        }
        ++photo;
      }

      if (sortByTitle && photosInSet.size())
      {
        map<string,string> reorderedIds;
        for (const auto& photo : photosInSet)
          reorderedIds[photo.second.title] = photo.first;

        vector<const char*> reorderedIdsVector;
        for (const auto& id : reorderedIds)
          reorderedIdsVector.emplace_back(id.second.c_str());
        reorderedIdsVector.emplace_back(nullptr);

        if (!dryRun)
        {
          if (auto ret = flickcurl_photosets_reorderPhotos(fc, setId.c_str(), reorderedIdsVector.data()))
            printf("ERROR: Unable to reorder photoset '%s' by photo/video titles: %d\n", setId.c_str(), ret);
          else
            printf("Photoset reordered '%s' by photo/video titles\n", setId.c_str());
        }
        else
          printf("Will reorder photoset '%s' by photo/video titles\n", setId.c_str());
      }

      printf("FlickrSync finished: Photos/videos in folder=%ld, Uploaded=%ld, Deleted=%d, Downloaded=%d, Photos/videos in Flickr set=%ld\n",
             photosInFolder.size(),
             uploadedPhotos.size(),
             deleted,
             downloaded,
             photosInSet.size());
    }
  }

tidy:
  if(fc)
    flickcurl_free(fc);

  flickcurl_finish();

  return(rc);
}
