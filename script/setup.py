from setuptools import setup, find_packages


from distutils.core import setup
setup(name='inject',
      version='0.1',
      packages=find_packages(),
      scripts=["inject/__main__.py"]
      )
