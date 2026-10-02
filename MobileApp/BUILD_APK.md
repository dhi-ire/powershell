# Build APK Guide

## Prerequisites
- Node.js 18+
- EAS CLI: `npm install -g eas-cli`
- Expo CLI: `npm install -g expo-cli`

## Quick Start

1. **Set Backend URL** in `App.js`:
   ```javascript
   const BACKEND_URL = 'http://your-backend.com/api';
   ```

2. **Install Dependencies**:
   ```bash
   npm install
   ```

3. **Test Locally**:
   ```bash
   npx expo start
   # Then press 'a' for Android or scan QR code with Expo Go app
   ```

## Build APK with EAS (Recommended)

### Option 1: Cloud Build (No Local Setup Required)
```bash
# Login to EAS
npx eas-cli login

# Build APK
npx eas-cli build --platform android --local=false

# Download APK from the link provided
```

### Option 2: Local Build
Requires Android Studio & SDK:
```bash
npx expo run:android
```

## Manual APK Build (Advanced)
```bash
# Generate native android directory
npx expo prebuild --clean

# Navigate to android folder
cd android

# Build APK with Gradle
./gradlew assembleRelease

# APK will be at: app/build/outputs/apk/release/app-release.apk
```

## Configuration

### Backend Connection
Edit `config.js` to change backend URL:
```javascript
export const API_CONFIG = {
  BACKEND_URL: 'https://your-api.com',
};
```

### App Package Name
Update in `app.json`:
```json
"android": {
  "package": "com.yourcompany.appname"
}
```

## APK Installation
```bash
adb install path/to/app-release.apk
```

## Troubleshooting

- **Connection Error**: Check backend URL in `App.js` and ensure API is accessible
- **Build Failed**: Run `npx expo doctor` to diagnose issues
- **Update SDK**: Run `npx expo install --fix` to fix dependency issues
