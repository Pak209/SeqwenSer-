# Getting Seqwenser onto an iPhone (TestFlight)

Not done yet, and nothing has been uploaded to App Store Connect. Needed:

1. Register the bundle id `com.dkimoto.seqwenser` (Apple Developer > Identifiers), team `5YJJCSFSQM`.
2. Create the app record in App Store Connect (name "Seqwenser" may be taken; pick a variant).
3. Signing: an Apple Distribution certificate and an App Store profile, or automatic signing driven by the App Store Connect API key.
4. Secrets: the ASC API key (key id, issuer id, .p8) and team id currently live only as secrets on `Pak209/pitchlane`. Either add them to this repo, or run the upload from a pitchlane workflow that checks out this repo at a pinned SHA.
5. Add a release workflow: `xcodegen generate`, `xcodebuild archive` (set `DEVELOPMENT_TEAM=5YJJCSFSQM`, `-allowProvisioningUpdates` with `-authenticationKeyPath/-authenticationKeyID/-authenticationKeyIssuerID`), `xcodebuild -exportArchive` with `method: app-store-connect` and `destination: upload`.
6. In App Store Connect, add Dan as an Internal Tester, install through the TestFlight app.

Each upload needs Dan's explicit go-ahead.
