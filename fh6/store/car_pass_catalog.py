"""Extract public FH6 Car Pass product metadata without contacting account services."""

import argparse
import json
from pathlib import Path
import re
import sys


PASS_ID = '9NTBFBZMB84T'
ID = re.compile(r'[A-Z0-9]{12}')
SKU = re.compile(r'[A-Z0-9]{4}')


def records(bundle, children, expected):
    parents = [row for row in bundle['Products'] if row['ProductId'] == PASS_ID]
    if len(parents) != 1:
        raise ValueError('Expected exactly one Microsoft Car Pass bundle product')
    parent = parents[0]
    bundled = {row['BigId'] for item in parent['DisplaySkuAvailabilities']
               for row in (item['Sku']['Properties'].get('BundledSkus') or [])}
    products = {row['ProductId']: row for row in children['Products']}
    products[PASS_ID] = parent
    if bundled | {PASS_ID} != set(products) or set(products) != expected or not bundled:
        raise ValueError('Car Pass bundle, product catalog and runtime allowlist disagree')
    for product_id, product in sorted(products.items()):
        if not ID.fullmatch(product_id) or product['ProductType'] != 'Durable':
            raise ValueError('Unexpected Car Pass product type or ID')
        sku_id = product['PreferredSkuId']
        skus = [item['Sku']['Properties'] for item in product['DisplaySkuAvailabilities']
                if item['Sku']['SkuId'] == sku_id]
        if (not SKU.fullmatch(sku_id) or len(skus) != 1 or skus[0].get('IsTrial') is not False
                or skus[0].get('Packages')):
            raise ValueError('Expected a public, unpackaged non-trial SKU: ' + product_id)
        culture = product['LocalizedProperties'][0]
        offer = product['Properties'].get('InAppOfferToken') or ''
        if product_id != PASS_ID and not offer:
            raise ValueError('Missing public vehicle offer token: ' + product_id)
        values = (product_id, sku_id, culture['Language'], offer, culture['ProductTitle'])
        line = '\t'.join(values)
        if any(any(ord(character) < 32 for character in field) for field in values) or len(line.encode()) >= 1023:
            raise ValueError('Invalid public catalog row: ' + product_id)
        yield line


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('children', type=Path)
    args = parser.parse_args()
    patch = Path(__file__).with_name('xgameruntime-fh6-car-pass.patch').read_text()
    allowlist = set(re.findall(r'"([A-Z0-9]{12})"', patch.split('static BOOL is_fh6_store', 1)[0]))
    sys.stdout.buffer.write(('\n'.join(records(json.loads(args.bundle.read_text()),
                                              json.loads(args.children.read_text()), allowlist)) + '\n').encode())
