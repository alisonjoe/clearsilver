/* Bring in gd library functions */
#include "gd.h"

/* Bring in standard I/O so we can output the PNG to a file */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <sys/fcntl.h>

#include "cgi/cgi.h"
#include "cgi/cgiwrap.h"
#include "util/neo_misc.h"

int CGIFinished = -1;

int jpeg_size (char *file, int *width, int *height)
{
  UINT8 data[64*1024];
  int blen;
  int fd;
  int pos;
  int length;
  UINT8 tag, marker;


  *width = 0; *height = 0;
  fd = open (file, O_RDONLY);
  if (fd == -1)
    return -1;

  blen = read(fd, data, sizeof(data));
  close(fd);
  pos = 2;
  while (pos+8 < blen)
  {
    tag = data[pos+0];
    if (tag != 0xff) return -1;
    marker = data[pos+1];
    length = data[pos+2] * 256 + data[pos+3] + 2;
    if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && 
	marker != 0xc8 && marker != 0xcc)
    {
      *height = data[pos+5] * 256 + data[pos+6];
      *width = data[pos+7] * 256 + data[pos+8];
      return 0;
    }
    pos += length;
  }
  return -1;
}

int isdir(char *dir) {
  struct stat statinfo;
  if ( stat(dir, &statinfo) != 0) {
    return 0;
  }

  return S_ISDIR(statinfo.st_mode);
}

int create_directories(char *fullpath) {
  char s[4000];
  char *last_slash;
  int last_slash_pos;

  if ((fullpath == NULL) || (strlen(fullpath) > 4000)) {
    return 1;
  }

  last_slash = strrchr(fullpath,'/');
  last_slash_pos = (last_slash - fullpath);
  /* fprintf(stderr,"dira(%d): %s\n", last_slash_pos,fullpath); */

  if (last_slash_pos > 2) {
    strncpy(s,fullpath,last_slash_pos);
    s[last_slash_pos] = 0;
    /* fprintf(stderr,"dir: %s\n", s); */

    if (!isdir(s)) {
      char s2[4000];
      sprintf(s2,"mkdir -p %s", s);
      return system(s2);
    }

  } else {
    return 1;
  }

  return 0;
}

void scale_and_display_image(char *fname,int maxW,int maxH,char *cachepath) {
  /* Declare the image */
  gdImagePtr src_im = 0;
  /* Declare input file */
  FILE *infile=0, *cachefile=0;
  int srcX,srcY,srcW,srcH;
  FILE *dispfile=0;

  /* if we can open the cachepath, then just print it */
  cachefile = fopen(cachepath,"rb");
  if (cachefile) {
    /* we should probably stat the files and make sure the thumbnail
       is current */
    /* fprintf(stderr,"using cachefile: %s\n",cachepath); */
    dispfile = cachefile;
  } else {
    char cmd[1024];
    int factor=1;

    if (jpeg_size (fname, &srcW, &srcH))
    {
      if ((srcW > maxW) || (srcH > maxH)) 
      {
	factor = 2;
	if (srcW / factor > maxW)
	{
	  factor = 4;
	  if (srcW / factor > maxW)
	    factor = 8;
	}
      }
    }

    snprintf (cmd, sizeof(cmd), "/usr/bin/djpeg -fast -scale 1/%d '%s' | /usr/bin/cjpeg -outfile '%s'", factor, fname, cachepath);

    create_directories(cachepath);
    system(cmd);
    cachefile = fopen(cachepath,"rb");
    if (cachefile) {
      dispfile = cachefile;

    } else /* no cachefile */ { 


      /* fprintf(stderr,"reading image\n"); */
      /* Read the image in */
      infile = fopen(fname,"rb");
      src_im = gdImageCreateFromJpeg(infile);
      srcX=0; srcY=0; srcW=src_im->sx; srcH=src_im->sy;


      /* figure out if we need to scale it */

      if ((srcW > maxW) || (srcH > maxH)) {
	/* scale paramaters */
	int dstX,dstY,dstW,dstH;
	/* Declare output file */
	FILE *jpegout;
	gdImagePtr dest_im;
	float srcAspect,dstAspect;

	/* create the destination image */

	dstX=0; dstY=0; 


	srcAspect = ((float)srcW/(float)srcH);
	dstAspect = ((float)maxW/(float)maxH);

	if (srcAspect == dstAspect) {
	  /* they are the same aspect ratio */
	  dstW = maxW;
	  dstH = maxH;
	} else if ( srcAspect > dstAspect ) {
	  /* if the src image has wider aspect ratio than the max */
	  dstW = maxW;
	  dstH = (int) ( ((float)dstW/(float)srcW) * srcH );
	} else {
	  /* if the src image has taller aspect ratio than the max */
	  dstH = maxW;
	  dstW = (int) ( ((float)dstH/(float)srcH) * srcW );
	}

	dest_im = gdImageCreate(dstW,dstH);

	/* fprintf(stderr,"scaling to (%d,%d)\n",dstW,dstH); */

	/* Scale it to the destination image */

	gdImageCopyResized(dest_im,src_im,dstX,dstY,srcX,srcY,dstW,dstH,srcW,srcH);

	/* fprintf(stderr,"scaling finished\n"); */

	/* write the output image */
	create_directories(cachepath);
	jpegout = fopen(cachepath,"wb+");
	if (!jpegout) {
	  jpegout = fopen("/tmp/foobar.jpg","wb+");
	}
	if (jpegout) {
	  gdImageJpeg(dest_im,jpegout,-1);
	  fflush(jpegout);

	  /* now print that data out the stream */
	  dispfile = jpegout;
	} else {
	  return;
	  /* couldn't open the output image so just print this without caching */
	  jpegout = stdout;
	  gdImageJpeg(dest_im,jpegout,-1);
	  fclose(jpegout);
	  gdImageDestroy(dest_im);

	  /* be sure to clean up everything! */
	  fclose(infile);
	  gdImageDestroy(src_im);
	  return;
	}


	gdImageDestroy(dest_im);

      } else {
	/* just print the input file because it's small enough */
	dispfile = infile;
      }

    }
  }

  /* the data in "dispfile" is going to be printed now */
  {

    char buf[4096];
    int count;
    
    fseek(dispfile,0,SEEK_SET);

    do {
      count = fread(buf,1,4096,dispfile);
      if (count > 0) {
	write(STDOUT_FILENO,buf,count); 
	
      }
    } while (count > 0);

  }

  if (dispfile) fclose(dispfile); 
  if (src_im) gdImageDestroy(src_im);

}

char *url_escape (char *buf)
{
  int nl = 0;
  int l = 0;
  char *s;

  while (buf[l])
  {
    if (buf[l] == '/' || buf[l] == '+' || buf[l] == '=' || buf[l] == '&' || 
	buf[l] == '"' ||
	buf[l] < 32 || buf[l] > 122)
    {
      nl += 2;
    }
    nl++;
    l++;
  }

  s = (char *) malloc (sizeof(char) * (nl + 1));
  if (s == NULL) return NULL;

  nl = 0; l = 0;
  while (buf[l])
  {
    if (buf[l] == ' ')
    {
      s[nl++] = '+';
      l++;
    }
    else
    if (buf[l] == '/' || buf[l] == '+' || buf[l] == '=' || buf[l] == '&' || 
	buf[l] == '"' ||
	buf[l] < 32 || buf[l] > 122)
    {
      s[nl++] = '%';
      s[nl++] = "0123456789ABCDEF"[buf[l] / 16];
      s[nl++] = "0123456789ABCDEF"[buf[l] % 16];
      l++;
    }
    else
    {
      s[nl++] = buf[l++];
    }
  }
  s[nl] = '\0';

  return s;
}

NEOERR *load_images (CGI *cgi, char *prefix, char *path, int limit)
{
  NEOERR *err = STATUS_OK;
  DIR *dp;
  struct dirent *de;
  char buf[256];
  char num[20];
  int i = 0;
  int l;
  int width, height;
  char ipath[_POSIX_PATH_MAX];

  if ((dp = opendir (path)) == NULL)
  {
    return nerr_raise(NERR_IO, "Unable to opendir %s: [%d] %s", path, errno, 
	strerror(errno));
  }
  while ((de = readdir (dp)) != NULL)
  {
    if (de->d_name[0] != '.')
    {
      l = strlen(de->d_name);
      if ((l>4 && !strcasecmp(de->d_name+l-4, ".jpg")) ||
	  (l>5 && !strcasecmp(de->d_name+l-5, ".jpeg")))
      {
	snprintf (buf, sizeof(buf), "%s.%d", prefix, i);
	err = hdf_set_buf (cgi->hdf, buf, url_escape(de->d_name));
	if (err != STATUS_OK) break;
	snprintf (ipath, sizeof(ipath), "%s/%s", path, de->d_name);
	if (!jpeg_size(ipath, &width, &height))
	{
	  snprintf (buf, sizeof(buf), "%s.%d.width", prefix, i);
	  snprintf (num, sizeof(num), "%d", width);
	  err = hdf_set_value (cgi->hdf, buf, num);
	  if (err != STATUS_OK) break;
	  snprintf (buf, sizeof(buf), "%s.%d.height", prefix, i);
	  snprintf (num, sizeof(num), "%d", height);
	  err = hdf_set_value (cgi->hdf, buf, num);
	  if (err != STATUS_OK) break;
	}
	i++;
	if (limit && i>=limit) break;
      }
    }
  }
  closedir(dp);
  return nerr_pass(err);
}

NEOERR *scale_images (CGI *cgi, char *prefix, int width, int height, int force)
{
  NEOERR *err;
  char num[20];
  HDF *obj;
  int i, x;
  int factor;

  obj = hdf_get_obj (cgi->hdf, prefix);
  if (obj) obj = hdf_obj_child (obj);
  while (obj)
  {
    factor = 1;
    i = hdf_get_int_value(obj, "width", -1);
    if (i != -1)
    {
      x = i;
      while (x > width)
      {
	/* factor = factor * 2;*/
	factor++;
	x = i / factor;
      }
      snprintf (num, sizeof(num), "%d", x);
      err = hdf_set_value (obj, "width", num);
      if (err != STATUS_OK) return nerr_pass (err);

      i = hdf_get_int_value(obj, "height", -1);
      if (i != -1)
      {
	i = i / factor;
	snprintf (num, sizeof(num), "%d", i);
	err = hdf_set_value (obj, "height", num);
	if (err != STATUS_OK) return nerr_pass (err);
      }
    }
    else
    {
      snprintf (num, sizeof(num), "%d", width);
      err = hdf_set_value (obj, "width", num);
      if (err != STATUS_OK) return nerr_pass (err);
      snprintf (num, sizeof(num), "%d", height);
      err = hdf_set_value (obj, "height", num);
      if (err != STATUS_OK) return nerr_pass (err);
    }
    obj = hdf_obj_next(obj);
  }
  return STATUS_OK;
}

NEOERR *dowork_picture (CGI *cgi, char *album, char *picture)
{
  NEOERR *err;
  char *base;
  char path[_POSIX_PATH_MAX];
  char buf[256];
  char buf2[256];
  char *image;
  char *enc_picture;
  int i, x, factor, y;
  int thumb_width, thumb_height;
  int pic_width, pic_height;

  base = hdf_get_value (cgi->hdf, "BASEDIR", NULL);
  if (base == NULL)
  {
    cgi_error (cgi, "No BASEDIR in imd file");
    return nerr_raise(CGIFinished, "Finished");
  }

  thumb_width = hdf_get_int_value (cgi->hdf, "ThumbWidth", 120);
  thumb_height = hdf_get_int_value (cgi->hdf, "ThumbWidth", 90);
  pic_width = hdf_get_int_value (cgi->hdf, "PictureWidth", 120);
  pic_height = hdf_get_int_value (cgi->hdf, "PictureWidth", 90);

  err = hdf_set_value (cgi->hdf, "Context", "picture");
  if (err != STATUS_OK) return nerr_pass(err);
  err = hdf_set_buf (cgi->hdf, "Album", url_escape(album));
  if (err != STATUS_OK) return nerr_pass(err);
  enc_picture = url_escape(picture);
  err = hdf_set_buf (cgi->hdf, "Picture", enc_picture);
  if (err != STATUS_OK) return nerr_pass(err);

  snprintf (path, sizeof(path), "%s/%s", base, album);
  err = load_images(cgi, "Images", path, 0);

  i = 0;
  snprintf (buf, sizeof(buf), "Images.%d", i);
  image = hdf_get_value(cgi->hdf, buf, NULL);
  while (image && strcmp(enc_picture, image))
  {
    i++;
    snprintf (buf, sizeof(buf), "Images.%d", i);
    image = hdf_get_value(cgi->hdf, buf, NULL);
  }

  if (image)
  {
    snprintf (buf, sizeof(buf), "Images.%d.width", i);
    x = hdf_get_int_value (cgi->hdf, buf, -1);
    if (x != -1)
    {
      factor = 1;
      y = x;
      while (y > pic_width)
      {
	/* factor = factor * 2; */
	factor++;
	y = x / factor;
      }
      snprintf (buf, sizeof(buf), "%d", y);
      hdf_set_value (cgi->hdf, "Picture.width", buf);
      snprintf (buf, sizeof(buf), "Images.%d.height", i);
      x = hdf_get_int_value (cgi->hdf, buf, -1);
      y = x / factor;
      snprintf (buf, sizeof(buf), "%d", y);
      hdf_set_value (cgi->hdf, "Picture.height", buf);
    }
    else
    {
      snprintf (buf, sizeof(buf), "%d", pic_width);
      hdf_set_value (cgi->hdf, "Picture.width", buf);
      snprintf (buf, sizeof(buf), "%d", pic_height);
      hdf_set_value (cgi->hdf, "Picture.height", buf);
    }

    i -= 2;
    for (x = 0; x<5; x++, i++)
    {
      if (i >= 0)
      {
	snprintf (buf, sizeof(buf), "Images.%d", i);
	snprintf (buf2, sizeof(buf2), "Show.%d", x);
	err = hdf_set_copy (cgi->hdf, buf2, buf);
	if (nerr_handle(&err, NERR_NOT_FOUND))
	{
	  /* pass */
	}
	else if (err != STATUS_OK) return nerr_pass (err);
	snprintf (buf, sizeof(buf), "Images.%d.width", i);
	snprintf (buf2, sizeof(buf2), "Show.%d.width", x);
	err = hdf_set_copy (cgi->hdf, buf2, buf);
	if (nerr_handle(&err, NERR_NOT_FOUND))
	{
	  /* pass */
	}
	else if (err != STATUS_OK) return nerr_pass (err);
	snprintf (buf, sizeof(buf), "Images.%d.height", i);
	snprintf (buf2, sizeof(buf2), "Show.%d.height", x);
	err = hdf_set_copy (cgi->hdf, buf2, buf);
	if (nerr_handle(&err, NERR_NOT_FOUND))
	{
	  /* pass */
	}
	else if (err != STATUS_OK) return nerr_pass (err);
      }
    }
    err = scale_images (cgi, "Show", thumb_width, thumb_height, 0);
    if (err != STATUS_OK) return nerr_pass (err);
  }

  return STATUS_OK;
}

NEOERR *dowork_album (CGI *cgi, char *album)
{
  NEOERR *err;
  char *base;
  char path[_POSIX_PATH_MAX];
  int thumb_width, thumb_height;

  base = hdf_get_value (cgi->hdf, "BASEDIR", NULL);
  if (base == NULL)
  {
    cgi_error (cgi, "No BASEDIR in imd file");
    return nerr_raise(CGIFinished, "Finished");
  }
  thumb_width = hdf_get_int_value (cgi->hdf, "ThumbWidth", 120);
  thumb_height = hdf_get_int_value (cgi->hdf, "ThumbWidth", 90);

  err = hdf_set_buf (cgi->hdf, "Album", url_escape(album));
  if (err != STATUS_OK) return nerr_pass(err);

  err = hdf_set_value (cgi->hdf, "Context", "album");
  if (err != STATUS_OK) return nerr_pass(err);

  snprintf (path, sizeof(path), "%s/%s", base, album);
  err = load_images(cgi, "Images", path, 0);
  if (err != STATUS_OK) return nerr_pass (err);
  err = scale_images (cgi, "Images", thumb_width, thumb_height, 0);
  if (err != STATUS_OK) return nerr_pass (err);

  return STATUS_OK;
}

NEOERR *dowork_album_overview (CGI *cgi)
{
  NEOERR *err;
  DIR *dp;
  char *album;
  struct dirent *de;
  char path[_POSIX_PATH_MAX];
  char buf[256];
  int i = 0;
  int thumb_width, thumb_height;

  album = hdf_get_value (cgi->hdf, "BASEDIR", NULL);
  if (album == NULL)
  {
    cgi_error (cgi, "No BASEDIR in imd file");
    return nerr_raise(CGIFinished, "Finished");
  }
  thumb_width = hdf_get_int_value (cgi->hdf, "ThumbWidth", 120);
  thumb_height = hdf_get_int_value (cgi->hdf, "ThumbWidth", 90);

  err = hdf_set_value (cgi->hdf, "Context", "overview");
  if (err != STATUS_OK) return nerr_pass(err);

  if ((dp = opendir (album)) == NULL)
  {
    return nerr_raise(NERR_IO, "Unable to opendir %s: [%d] %s", album, errno, 
	strerror(errno));
  }

  while ((de = readdir (dp)) != NULL)
  {
    if (de->d_name[0] != '.')
    {
      snprintf(path, sizeof(path), "%s/%s", album, de->d_name);
      if (isdir(path))
      {
	snprintf(buf, sizeof(buf), "Albums.%d", i);
	err = hdf_set_buf (cgi->hdf, buf, url_escape(de->d_name));
	if (err != STATUS_OK) break;
	snprintf(buf, sizeof(buf), "Albums.%d.Images", i);
	err = load_images(cgi, buf, path, 4);
	if (err != STATUS_OK) break;
	err = scale_images (cgi, buf, thumb_width, thumb_height, 0);
	if (err != STATUS_OK) break;
	i++;
      }
    }
  }
  closedir(dp);
  return nerr_pass(err);
}

NEOERR *dowork_image (CGI *cgi, char *image) 
{
  int maxW = 1024, maxH = 1024;
  char *basepath = "/home/blong/public_html/Images/";
  char *cache_basepath = "/tmp/.imgcache/";
  char srcpath[_POSIX_PATH_MAX] = "";
  char cachepath[_POSIX_PATH_MAX] = "";
  char *header;
  int i;

  if ((i = hdf_get_int_value(cgi->hdf, "Query.width", 0)) != 0) {
    maxW = i;
  }

  if ((i = hdf_get_int_value(cgi->hdf, "Query.height", 0)) != 0) {
    maxH = i;
  }

  basepath = hdf_get_value(cgi->hdf, "BASEDIR", NULL);
  if (basepath == NULL)
  {
    cgi_error (cgi, "No BASEDIR in imd file");
    return nerr_raise(CGIFinished, "Finished");
  }

  snprintf (srcpath, sizeof(srcpath), "%s/%s", basepath, image);
  snprintf (cachepath, sizeof(cachepath), "%s/%dx%d/%s", cache_basepath, 
      maxW, maxH,image);

  /* fprintf(stderr,"cachepath: %s\n",cachepath); */

  /* write the HTTP header! */
  header = "Content-Type: image/jpeg\n\n";
  fwrite(header,strlen(header),1,stdout);
  fflush(stdout);

  scale_and_display_image(srcpath,maxW,maxH,cachepath);
  return STATUS_OK;
}

int main(int argc, char **argv, char **envp)
{
  NEOERR *err;
  CGI *cgi;
  char *image;
  char *album;
  char *imd_file;
  char *cs_file;
  char *picture;

  cgi_debug_init (argc,argv);
  cgiwrap_init_std (argc, argv, envp);
  
  nerr_init();
  nerr_register(&CGIFinished, "CGIFinished");

  err = cgi_init(&cgi, NULL);
  if (err != STATUS_OK)
  {
    cgi_neo_error(cgi, err);
    nerr_log_error(err);
    return -1;
  }
  imd_file = hdf_get_value(cgi->hdf, "CGI.PathTranslated", NULL);
  err = hdf_read_file (cgi->hdf, imd_file);
  if (err != STATUS_OK)
  {
    cgi_neo_error(cgi, err);
    nerr_log_error(err);
    return -1;
  }

  cs_file = hdf_get_value(cgi->hdf, "Template", NULL);
  image = hdf_get_value(cgi->hdf, "Query.image", NULL);
  album = hdf_get_value(cgi->hdf, "Query.album", NULL);
  picture = hdf_get_value(cgi->hdf, "Query.picture", NULL);
  if (image)
    dowork_image(cgi, image);
  else 
  {
    if (!album)
    {
      err = dowork_album_overview (cgi);
    }
    else if (!picture)
    {
      err = dowork_album (cgi, album);
    }
    else
    {
      err = dowork_picture (cgi, album, picture);
    }
    if (err != STATUS_OK)
    {
      if (nerr_handle(&err, CGIFinished))
      {
	/* pass */
      }
    }
    else
    {
      err = cgi_display(cgi, cs_file);
      if (err != STATUS_OK)
      {
	cgi_neo_error(cgi, err);
	nerr_log_error(err);
	return -1;
      }
    }
  }
  return 0;
}