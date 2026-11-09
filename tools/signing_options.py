"""Derive Xcode signing/export settings from the supplied provisioning profile."""
import datetime
from pathlib import Path
import plistlib
import sys


def export_options(profile, bundle_id):
    team = profile["TeamIdentifier"][0]
    entitlements = profile["Entitlements"]
    application_id = entitlements["application-identifier"]
    allowed = application_id.split(".", 1)[1]
    if allowed != bundle_id and not (allowed.endswith("*") and bundle_id.startswith(allowed[:-1])):
        raise ValueError("Provisioning profile does not match IOS_BUNDLE_ID")
    if profile["ExpirationDate"] <= datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None):
        raise ValueError("Provisioning profile has expired")
    method = "debugging" if entitlements.get("get-task-allow") else (
        "enterprise" if profile.get("ProvisionsAllDevices") else
        "release-testing" if profile.get("ProvisionedDevices") else "app-store-connect")
    return {"method": method, "signingStyle": "manual", "teamID": team,
            "provisioningProfiles": {bundle_id: profile["UUID"]}}


if __name__ == "__main__":
    with Path(sys.argv[1]).open("rb") as source:
        profile = plistlib.load(source)
    with Path(sys.argv[2]).open("wb") as target:
        plistlib.dump(export_options(profile, sys.argv[3]), target)
