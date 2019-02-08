#pragma once
#include "BlockingQueue.h"
#include "DbAlbumCollection.h"
#include "Helpers.h"
#include "Image.h"

using namespace boost::multi_index;

class ImgTexture;
class EngineThread;

struct TextureCacheMeta {
  std::string groupString;
  unsigned int collectionVersion;
  // (generation, -distance to center)
  std::pair<unsigned int, int> priority;
};

class TextureLoadingThreads {
 public:
  TextureLoadingThreads();
  ~TextureLoadingThreads();

  struct LoadRequest {
    TextureCacheMeta meta;
    metadb_handle_list tracks;
  };

  struct LoadResponse {
    TextureCacheMeta meta;
    std::optional<UploadReadyImage> image;

    LoadResponse(LoadResponse&& other)
        : meta(other.meta), image(std::move(other.image)){};
    LoadResponse(const TextureCacheMeta& meta, std::optional<UploadReadyImage>&& image)
        : meta(meta), image(std::move(image)){};
  };

  void flushQueue();
  void enqueue(const metadb_handle_list& tracks, const TextureCacheMeta& meta);
  std::optional<LoadResponse> getLoaded();

 private:
  std::vector<std::thread> threads;
  std::atomic<bool> shouldStop = false;

  BlockingQueue<LoadRequest> inQueue;
  BlockingQueue<LoadResponse> outQueue;

  void run();
};

class TextureCache {
  DbAlbumCollection& db;
  EngineThread& thread;

 public:
  TextureCache(EngineThread&, DbAlbumCollection&);

  const GLTexture& getLoadedImgTexture(const std::string& albumName);

  void trimCache();
  void clearCache();
  void onTargetChange();
  void onCollectionReload();
  void updateLoadingQueue(const CollectionPos& queueCenter);
  void uploadTextures();

 private:
  unsigned int collectionVersion = 0;

  void reloadSpecialTextures();
  GLTexture noCoverTexture;
  GLTexture loadingTexture;

  unsigned int cacheGeneration = 0;

  struct CacheItem : TextureCacheMeta {
    CacheItem(const TextureCacheMeta& meta, std::optional<GLTexture>&& texture)
        : TextureCacheMeta(meta), texture(std::move(texture)){};
    std::optional<GLTexture> texture;
  };
  typedef multi_index_container<
      CacheItem,
      indexed_by<
          hashed_unique<member<TextureCacheMeta, std::string, &CacheItem::groupString>>,
          ordered_non_unique<composite_key<
              CacheItem,
              member<TextureCacheMeta, unsigned int, &CacheItem::collectionVersion>,
              member<TextureCacheMeta, std::pair<unsigned int, int>,
                     &CacheItem::priority>>>>>
      t_textureCache;

  t_textureCache textureCache;

  TextureLoadingThreads bgLoader;

  friend class TextureLoadingThreads;
};
