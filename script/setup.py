from setuptools import setup, find_packages


from distutils.core import setup
setup(name='inject',
      version='0.2',
      packages=find_packages(),
      scripts=["inject/__main__.py"],
      entry_points={
      'console_scripts': [
          'inject=inject:main',
    ]
      }
)
