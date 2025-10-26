# Recent M5Stick Firmware Changes

## 🎨 New Feature: Sprite Viewer in Debug Menu

**Access**: Press C button → Debug Menu → "Sprites"

- View all sprites one by one (Bailey & Gengar)
- Press A to cycle through sprites
- Press B to exit
- Shows sprite name in top-left corner

## 📊 Enhanced "SENT" Screen with API Stats

The success screen after uploading now shows **much more information**:

### Currently Displays (with old API):

- ✅ Duration (calculated from file size)
- ✅ File size in KB

### Will Display (once you update the API):

- ✅ **Category** (task, note, idea, etc.)
- ✅ **Duration** (from API, more accurate)
- ✅ **File size** (from API)
- ✅ **Tokens used** (Whisper + LLM)
- ✅ **Quota remaining** (e.g., "4600 left") or percent used
- ✅ **Processing time** (API round-trip in seconds)

### How It Works:

1. M5Stick sends recording to `/api/transcribe/device`
2. API responds with enhanced JSON (once you implement it)
3. M5Stick parses the JSON and displays the stats
4. User sees immediate feedback about their recording

### Graceful Fallback:

- If API doesn't return new fields yet, M5Stick calculates duration locally
- Old API responses still work, just show less info
- No breaking changes!

---

## 🔧 What You Need to Do

See `API_RESPONSE_ENHANCEMENTS.md` for complete instructions.

**TL;DR:**

1. Create Supabase migration for `daily_usage` table
2. Update `/app/api/transcribe/device/route.ts` to return enhanced response
3. Test with M5Stick!

**Example enhanced response:**

```json
{
  "success": true,
  "category": "task",
  "recording": {
    "duration_seconds": 15.5,
    "file_size_kb": 484
  },
  "tokens": {
    "total": 600
  },
  "quota": {
    "remaining": 4600,
    "percent_used": 54
  },
  "processing_time_ms": 1234
}
```

---

## 📋 Other Changes

- Lowered screen brightness from 255 → 50 (you edited this)
- Added `lastApiResponse` global variable to store API responses
- Added `UploadResponse` struct to hold parsed data
- Added `parseUploadResponse()` function for JSON parsing
- Success screen delay increased to 2.5s (from 1.5s) to show more stats

---

## 🧪 Testing

1. **Test Sprite Viewer**: Press C → Debug → Sprites → Press A to cycle
2. **Test Current "SENT" Screen**: Record → Upload → See duration & file size
3. **Test Enhanced "SENT" Screen** (after API update): Should show all stats

---

## 💡 Future Enhancements

Once the API is updated, you could also add:

- Daily usage indicator on ready screen
- Warning when approaching token limit
- Color-coded quota (green/yellow/red)
- Memo ID display (for debugging)
- Confidence score display
