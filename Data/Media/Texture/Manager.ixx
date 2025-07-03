export module GW2Viewer.Data.Media.Texture.Manager;
import GW2Viewer.Common;
import GW2Viewer.Data.Media.Texture;
import GW2Viewer.System.Graphics;
import std;
import <concurrentqueue/blockingconcurrentqueue.h>;
import <gsl/util>;

using namespace std::chrono_literals;

export namespace GW2Viewer::Data::Media::Texture
{

class Manager
{
public:
    TextureEntry const* Get(uint32 fileID)
    {
        std::scoped_lock _(m_mutex);
        if (auto const itr = m_textures.find(fileID); itr != m_textures.end())
            return itr->second.get();

        return nullptr;
    }
    std::unique_ptr<Texture> Create(uint32 width, uint32 height, void const* data = nullptr);
    void Load(uint32 fileID, LoadTextureOptions const& options = { });
    void UploadToGPU();
    void StopLoading()
    {
        m_loadingThreadExitRequested = true;
        if (m_loadingThread)
            m_loadingThread->join();
    }

private:
    std::unordered_map<uint32, std::shared_ptr<TextureEntry>> m_textures;
    std::recursive_mutex m_mutex;

    struct BoxedImage { void* ScratchImage; ~BoxedImage(); };
    std::unique_ptr<BoxedImage> GetTextureRGBAImage(TextureEntry& texture);

    moodycamel::BlockingConcurrentQueue<std::weak_ptr<TextureEntry>> m_loadingQueue;
    moodycamel::ConcurrentQueue<std::pair<std::weak_ptr<TextureEntry>, std::unique_ptr<BoxedImage>>> m_uploadQueue;

    std::optional<std::thread> m_loadingThread;
    bool m_loadingThreadExitRequested = false;

    void LoadingThread();
};

}
