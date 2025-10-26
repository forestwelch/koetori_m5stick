# API Response Enhancements for M5Stick Display

## What's Been Done

✅ **M5Stick firmware updated** to parse and display enhanced API response  
✅ **Sprite viewer** added to debug menu (view Bailey & Gengar sprites)  
✅ **Success screen** now shows: category, duration, file size, tokens, quota, and processing time

## What YOU Need to Implement in the API

The M5Stick is now ready to display enhanced stats, but your API (`app/api/transcribe/device/route.ts`) needs to be updated to return them.

---

## 1. Add Supabase Migration for Daily Usage Tracking

Create: `supabase/migrations/20241027000001_add_daily_usage.sql`

```sql
CREATE TABLE IF NOT EXISTS daily_usage (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  username TEXT NOT NULL,
  date DATE NOT NULL DEFAULT CURRENT_DATE,
  tokens_used INTEGER NOT NULL DEFAULT 0,
  requests_count INTEGER NOT NULL DEFAULT 0,
  created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
  updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
  UNIQUE(username, date)
);

CREATE INDEX idx_daily_usage_username_date ON daily_usage(username, date);

COMMENT ON TABLE daily_usage IS 'Track daily token usage per user for quota management';
```

Run: `npx supabase migration up`

---

## 2. Update `/app/api/transcribe/device/route.ts`

### Add Constants at Top

```typescript
const DAILY_TOKEN_LIMIT = 10000; // Adjust as needed
```

### After Line 83: Calculate Recording Duration

```typescript
const audioFile = formData.get("audio");
const deviceId = (formData.get("device_id") as string | null) || "unknown";
const username = formData.get("username") as string | null;

// Calculate recording duration from file size
const fileSizeBytes = audioFile.size;
const durationSeconds = (fileSizeBytes - 44) / (16000 * 2); // 16kHz, 16-bit mono, minus 44-byte WAV header
```

### After Line 144: Capture Whisper Token Usage

```typescript
const transcription = await groq.audio.transcriptions.create({
  file: file,
  model: "whisper-large-v3-turbo",
  language: "en",
  response_format: "json",
});

// Capture token usage (check if Groq provides this - may need to estimate)
const whisperTokens =
  transcription.x_groq?.usage?.total_tokens || Math.ceil(durationSeconds * 50); // Estimate: ~50 tokens/second
```

### After Line 173: Capture LLM Token Usage

```typescript
const categorizationResponse = await groq.chat.completions.create({
  messages: [{ role: "user", content: categorizationPrompt }],
  model: "llama-3.3-70b-versatile",
  temperature: 0.3,
  response_format: { type: "json_object" },
});

// Capture LLM token usage
const llmTokens = categorizationResponse.usage?.total_tokens || 0;
const totalTokens = whisperTokens + llmTokens;
```

### After Line 200: Track Daily Usage

```typescript
const { data: memoData, error: supabaseError } = await supabase
  .from("memos")
  .insert({
    /* ... existing fields ... */
  })
  .select()
  .single();

if (supabaseError) {
  // ... existing error handling ...
}

// Track daily token usage
const today = new Date().toISOString().split("T")[0]; // YYYY-MM-DD

// Get today's usage (if exists)
const { data: usageData } = await supabase
  .from("daily_usage")
  .select("tokens_used, requests_count")
  .eq("username", finalUsername)
  .eq("date", today)
  .single();

const tokensUsedToday = (usageData?.tokens_used || 0) + totalTokens;
const requestsCount = (usageData?.requests_count || 0) + 1;

// Upsert usage record
await supabase.from("daily_usage").upsert(
  {
    username: finalUsername,
    date: today,
    tokens_used: tokensUsedToday,
    requests_count: requestsCount,
    updated_at: new Date().toISOString(),
  },
  {
    onConflict: "username,date",
  }
);

const tokensRemaining = Math.max(0, DAILY_TOKEN_LIMIT - tokensUsedToday);
const percentUsed = Math.min(
  100,
  Math.round((tokensUsedToday / DAILY_TOKEN_LIMIT) * 100)
);
```

### Replace Lines 224-232: Enhanced Response

```typescript
// Return enhanced response for device
return NextResponse.json({
  success: true,
  memo_id: memoData?.id,
  category: categorization.category,
  confidence: categorization.confidence,
  needs_review: needsReview,
  processing_time_ms: processingTime,

  // NEW: Recording metadata
  recording: {
    duration_seconds: parseFloat(durationSeconds.toFixed(1)),
    file_size_bytes: fileSizeBytes,
    file_size_kb: Math.round(fileSizeBytes / 1024),
  },

  // NEW: Token usage
  tokens: {
    whisper: whisperTokens,
    llm: llmTokens,
    total: totalTokens,
  },

  // NEW: Daily quota info
  quota: {
    used_today: tokensUsedToday,
    daily_limit: DAILY_TOKEN_LIMIT,
    remaining: tokensRemaining,
    percent_used: percentUsed,
  },
});
```

---

## 3. Test the Enhanced Response

1. **Run the migration**: Create the `daily_usage` table
2. **Deploy updated API**
3. **Record and send from M5Stick**
4. **Check success screen** - should now show:
   - Category (e.g., "task", "note", "idea")
   - Duration & file size
   - Tokens used
   - Quota remaining or percent used
   - Processing time

---

## Example Enhanced "SENT" Screen

```
╔═══════════════════════════╗
║                           ║
║        SENT ✓             ║
║                           ║
║  Type: task               ║
║  15.5s / 484KB            ║
║  Tokens: 600              ║
║  Quota: 4600 left         ║
║  Time: 1.2s               ║
║                           ║
╚═══════════════════════════╝
```

---

## Benefits

- **Transparency**: Users see exactly what happened with their recording
- **Quota awareness**: Users know when they're approaching limits
- **Category feedback**: Confirms the AI understood the recording type
- **Debugging**: Duration/size helps identify recording issues
- **Token tracking**: Prepare for usage-based billing or limits

---

## Backwards Compatibility

The M5Stick code gracefully handles both old and new API responses:

- If new fields are missing, it falls back to calculating duration from file size
- Old API responses (without enhanced fields) will still work, just showing less info
