package org.libreoffice;

import android.graphics.Bitmap;

import org.mozilla.gecko.gfx.BufferedCairoImage;
import org.mozilla.gecko.gfx.CairoImage;
import org.mozilla.gecko.gfx.GeckoLayerClient;
import org.mozilla.gecko.gfx.IntSize;

public class MockTileProvider implements TileProvider {
    private static final int TILE_SIZE = 256;
    private final GeckoLayerClient mLayerClient;
    private final String inputFile;

    public MockTileProvider(GeckoLayerClient layerClient, String input) {
        mLayerClient = layerClient;
        this.inputFile = input;

        for (int i = 0; i < 5; i++) {
            String partName = "Part " + i;
            DocumentPartView partView = new DocumentPartView(i, partName, null);
            LibreOfficeMainActivity.mAppContext.getDocumentPartViewListAdapter().add(partView);
        }
        LibreOfficeMainActivity.mAppContext.mMainHandler.post(new Runnable() {
            @Override
            public void run() {
                LibreOfficeMainActivity.mAppContext.getDocumentPartViewListAdapter().notifyDataSetChanged();
            }
        });
    }

    @Override
    public int getPageWidth() {
        return 549;
    }

    @Override
    public int getPageHeight() {
        return 630 * 5;
    }

    @Override
    public boolean isReady() {
        return true;
    }

    @Override
    public CairoImage createTile(float x, float y, IntSize tileSize, float zoom) {
        int tiles = (int) (getPageWidth() / TILE_SIZE) + 1;
        int tileNumber = (int) ((y / TILE_SIZE) * tiles + (x / TILE_SIZE));
        tileNumber %= 9;
        tileNumber += 1; // 0 to 1 based numbering

        String imageName = "d" + tileNumber;
        Bitmap bitmap = mLayerClient.getView().getDrawable(imageName);

        CairoImage image = new BufferedCairoImage(bitmap);

        return image;
    }

    @Override
    public Bitmap thumbnail(int size) {
        return mLayerClient.getView().getDrawable("dummy_page");
    }

    @Override
    public void close() {
    }

    @Override
    public void changePart(int partIndex) {

    }
}
